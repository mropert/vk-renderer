#include "command_buffer.h"

#include <renderer/texture.h>

void renderer::CommandBuffer::begin()
{
	_cmd_buffer.begin( vk::CommandBufferBeginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
}

void renderer::CommandBuffer::end()
{
	_cmd_buffer.end();
}

void renderer::CommandBuffer::reset()
{
	_cmd_buffer.reset();
}

void renderer::CommandBuffer::begin_rendering( Extent2D extent, RenderAttachment color_target, RenderAttachment depth_target )
{
	const vk::RenderingAttachmentInfo color_attachment { .imageView = color_target.target->_view,
														 .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
														 .loadOp = vk::AttachmentLoadOp::eClear,
														 .storeOp = vk::AttachmentStoreOp::eStore,
														 .clearValue = { .color = color_target.clear_value } };

	vk::RenderingAttachmentInfo depth_attachment;
	if ( depth_target.target )
	{
		depth_attachment = { .imageView = depth_target.target->_view,
							 .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
							 .loadOp = vk::AttachmentLoadOp::eClear,
							 .storeOp = vk::AttachmentStoreOp::eStore };
	}

	const vk::RenderingInfo renderInfo { .renderArea = vk::Rect2D { .extent = extent },
										 .layerCount = 1,
										 .colorAttachmentCount = 1,
										 .pColorAttachments = &color_attachment,
										 .pDepthAttachment = depth_target.target ? &depth_attachment : nullptr };

	_cmd_buffer.beginRendering( renderInfo );
}

void renderer::CommandBuffer::end_rendering()
{
	_cmd_buffer.endRendering();
}

void renderer::CommandBuffer::transition_texture( Texture& tex, Texture::Layout target )
{
	const vk::ImageAspectFlags aspectMask = ( target == Texture::Layout::DEPTH_ATTACHMENT_OPTIMAL
											  || target == Texture::Layout::DEPTH_READ_ONLY_OPTIMAL )
		? vk::ImageAspectFlagBits::eDepth
		: vk::ImageAspectFlagBits::eColor;
	const vk::ImageMemoryBarrier2 imageBarrier { .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
												 .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
												 .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
												 .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
												 .oldLayout = static_cast<vk::ImageLayout>( tex._layout ),
												 .newLayout = static_cast<vk::ImageLayout>( target ),
												 .image = tex._image,
												 .subresourceRange = {
													 .aspectMask = aspectMask,
													 .levelCount = VK_REMAINING_MIP_LEVELS,
													 .layerCount = VK_REMAINING_ARRAY_LAYERS,
												 } };

	_cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imageBarrier } );
	tex._layout = target;
}
