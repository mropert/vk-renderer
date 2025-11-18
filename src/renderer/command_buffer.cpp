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
	vk::RenderingAttachmentInfo color_attachment { .imageView = color_target.target._view,
												   .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
												   .loadOp = vk::AttachmentLoadOp::eClear,
												   .storeOp = vk::AttachmentStoreOp::eStore,
												   .clearValue = { .color = color_target.clear_value } };

	if ( color_target.resolve_target._view )
	{
		color_attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
		color_attachment.resolveImageView = color_target.resolve_target._view;
		color_attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	}

	vk::RenderingAttachmentInfo depth_attachment;
	if ( depth_target.target._view )
	{
		depth_attachment = { .imageView = depth_target.target._view,
							 .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
							 .loadOp = vk::AttachmentLoadOp::eClear,
							 .storeOp = vk::AttachmentStoreOp::eStore };
	}

	const vk::RenderingInfo renderInfo { .renderArea = vk::Rect2D { .extent = extent },
										 .layerCount = 1,
										 .colorAttachmentCount = 1,
										 .pColorAttachments = &color_attachment,
										 .pDepthAttachment = depth_target.target._view ? &depth_attachment : nullptr };

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

void renderer::CommandBuffer::set_scissor( Extent2D extent )
{
	const vk::Rect2D scissor { .extent = extent };
	_cmd_buffer.setScissor( 0, scissor );
}

void renderer::CommandBuffer::set_viewport( Extent2D extent )
{
	const vk::Viewport viewport { .x = 0.f,
								  .y = 0.f,
								  .width = static_cast<float>( extent.width ),
								  .height = static_cast<float>( extent.height ),
								  .minDepth = 0.f,
								  .maxDepth = 1.f };

	_cmd_buffer.setViewport( 0, viewport );
}
