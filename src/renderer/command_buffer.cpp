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
