#include "command_buffer.h"

#include <cassert>
#include <renderer/bindless.h>
#include <renderer/buffer.h>
#include <renderer/details/profiler.h>
#include <renderer/pipeline.h>
#include <renderer/texture.h>

void renderer::CommandBuffer::begin()
{
	_cmd_buffer.begin( vk::CommandBufferBeginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
#ifdef USE_OPTICK
	_optick_previous = Optick::SetGpuContext( Optick::GPUContext( static_cast<VkCommandBuffer>( *_cmd_buffer ) ) ).cmdBuffer;
#endif
}

void renderer::CommandBuffer::end()
{
	_cmd_buffer.end();
#ifdef USE_OPTICK
	Optick::SetGpuContext( Optick::GPUContext( _optick_previous ) );
	_optick_previous = nullptr;
#endif
}

void renderer::CommandBuffer::reset()
{
	_cmd_buffer.reset();
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

void renderer::CommandBuffer::blit_texture( const Texture& src, const Texture& dst )
{
	assert( src._layout == Texture::Layout::TRANSFER_SRC_OPTIMAL );
	assert( dst._layout == Texture::Layout::TRANSFER_DST_OPTIMAL );

	const vk::ImageBlit2 blit_region { .srcSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
									   .srcOffsets = { { vk::Offset3D {}, vk::Offset3D( src._extent.width, src._extent.height, 1 ) } },
									   .dstSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
									   .dstOffsets = { { vk::Offset3D {}, vk::Offset3D( dst._extent.width, dst._extent.height, 1 ) } } };

	const vk::BlitImageInfo2 blit_info { .srcImage = src._image,
										 .srcImageLayout = static_cast<vk::ImageLayout>( src._layout ),
										 .dstImage = dst._image,
										 .dstImageLayout = static_cast<vk::ImageLayout>( dst._layout ),
										 .regionCount = 1,
										 .pRegions = &blit_region };

	_cmd_buffer.blitImage2( blit_info );
}

void renderer::CommandBuffer::copy_buffer( const Buffer& src,
										   std::size_t offset,
										   std::size_t size,
										   const Buffer& dest,
										   std::size_t dest_offset )
{
	_cmd_buffer.copyBuffer( src._buffer, dest._buffer, vk::BufferCopy { .srcOffset = offset, .dstOffset = dest_offset, .size = size } );
}

void renderer::CommandBuffer::copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex )
{
	_cmd_buffer.copyBufferToImage(
		buffer._buffer,
		tex._image,
		static_cast<vk::ImageLayout>( tex._layout ),
		vk::BufferImageCopy { .bufferOffset = offset,
							  .imageSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
							  .imageExtent = vk::Extent3D { .width = tex._extent.width, .height = tex._extent.height, .depth = 1 } } );
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

void renderer::CommandBuffer::bind_pipeline( const Pipeline& pipeline, const BindlessManager& bindless_manager )
{
	_cmd_buffer.bindPipeline( vk::PipelineBindPoint::eGraphics, pipeline._pipeline );
	_cmd_buffer.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, pipeline._layout, 0, bindless_manager.get_set(), {} );
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

void renderer::CommandBuffer::bind_index_buffer( const Buffer& index_buffer )
{
	assert( ( index_buffer._usage & Buffer::Usage::INDEX_BUFFER ) == Buffer::Usage::INDEX_BUFFER );
	_cmd_buffer.bindIndexBuffer( index_buffer._buffer, 0, vk::IndexType::eUint32 );
}

void renderer::CommandBuffer::draw( uint32_t count )
{
	_cmd_buffer.draw( count, 1, 0, 0 );
}

void renderer::CommandBuffer::draw_indexed( uint32_t count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance )
{
	_cmd_buffer.drawIndexed( count, instance_count, first_index, 0, first_instance );
}

void renderer::CommandBuffer::push_constants( const Pipeline& pipeline, const void* data, std::size_t size )
{
	assert( pipeline._desc.push_constants_size == size );
	_cmd_buffer.getDispatcher()
		->vkCmdPushConstants( *_cmd_buffer, pipeline._layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, static_cast<uint32_t>( size ), data );
}
