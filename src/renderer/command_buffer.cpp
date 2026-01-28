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

void renderer::CommandBuffer::texture_barrier( const Texture& tex,
											   Texture::Layout src_layout,
											   Texture::Layout dst_layout,
											   vk::PipelineStageFlags2 src_stage,
											   vk::PipelineStageFlags2 dst_stage,
											   vk::AccessFlags2 src_access,
											   vk::AccessFlags2 dst_access,
											   int mip_level )
{
	assert( mip_level == -1 || mip_level < tex.get_mips() );

	const vk::ImageAspectFlags aspectMask = ( tex.get_format() == Texture::Format::D32_SFLOAT ) ? vk::ImageAspectFlagBits::eDepth
																								: vk::ImageAspectFlagBits::eColor;
	const vk::ImageMemoryBarrier2 imageBarrier { .srcStageMask = src_stage,
												 .srcAccessMask = src_access,
												 .dstStageMask = dst_stage,
												 .dstAccessMask = dst_access,
												 .oldLayout = static_cast<vk::ImageLayout>( src_layout ),
												 .newLayout = static_cast<vk::ImageLayout>( dst_layout ),
												 .image = tex._image,
												 .subresourceRange = {
													 .aspectMask = aspectMask,
													 .baseMipLevel = static_cast<uint32_t>( mip_level == -1 ? 0 : mip_level ),
													 .levelCount = mip_level == -1 ? VK_REMAINING_MIP_LEVELS : 1,
													 .layerCount = VK_REMAINING_ARRAY_LAYERS,
												 } };

	_cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imageBarrier } );
}

void renderer::CommandBuffer::transition_texture( const Texture& tex,
												  Texture::Layout src_layout,
												  Texture::Layout dst_layout,
												  int mip_level )
{
	// XXX: _extremely_ conservative barrier
	texture_barrier( tex,
					 src_layout,
					 dst_layout,
					 vk::PipelineStageFlagBits2::eAllCommands,
					 vk::PipelineStageFlagBits2::eAllCommands,
					 vk::AccessFlagBits2::eMemoryWrite,
					 vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
					 mip_level );
}

void renderer::CommandBuffer::blit_texture( const Texture& src, const Texture& dst )
{
	const vk::ImageBlit2 blit_region {
		.srcSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
		.srcOffsets = { { vk::Offset3D {}, vk::Offset3D( src._desc.extent.width, src._desc.extent.height, 1 ) } },
		.dstSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
		.dstOffsets = { { vk::Offset3D {}, vk::Offset3D( dst._desc.extent.width, dst._desc.extent.height, 1 ) } }
	};

	const vk::BlitImageInfo2 blit_info { .srcImage = src._image,
										 .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
										 .dstImage = dst._image,
										 .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
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
		vk::ImageLayout::eTransferDstOptimal,
		vk::BufferImageCopy {
			.bufferOffset = offset,
			.imageSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
			.imageExtent = vk::Extent3D { .width = tex._desc.extent.width, .height = tex._desc.extent.height, .depth = 1 } } );
}

void renderer::CommandBuffer::fill_buffer( const Buffer& buffer, size_t offset, size_t size, uint32_t value )
{
	assert( offset + size <= buffer.get_size() );
	_cmd_buffer.fillBuffer( buffer.get_buffer(), offset, size, value );
}

void renderer::CommandBuffer::buffer_barrier( const Buffer& buffer )
{
	// XXX: aggressive write -> read barrier
	buffer_barrier( buffer,
					vk::PipelineStageFlagBits2::eAllCommands,
					vk::PipelineStageFlagBits2::eAllCommands,
					vk::AccessFlagBits2::eMemoryWrite,
					vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite );
}

void renderer::CommandBuffer::buffer_barrier( const Buffer& buffer,
											  vk::PipelineStageFlags2 src_stage,
											  vk::PipelineStageFlags2 dst_stage,
											  vk::AccessFlags2 src_access,
											  vk::AccessFlags2 dst_access )
{
	const vk::BufferMemoryBarrier2 bufferBarrier { .srcStageMask = src_stage,
												   .srcAccessMask = src_access,
												   .dstStageMask = dst_stage,
												   .dstAccessMask = dst_access,
												   .buffer = buffer.get_buffer(),
												   .offset = 0,
												   .size = buffer.get_size() };

	_cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &bufferBarrier } );
}

void renderer::CommandBuffer::begin_rendering( Extent2D extent, RenderAttachment color_target, RenderAttachment depth_target )
{
	vk::RenderingAttachmentInfo color_attachment { .imageView = color_target.target._view,
												   .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
												   .storeOp = vk::AttachmentStoreOp::eStore };
	if ( color_target.clear_value )
	{
		color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
		color_attachment.clearValue = { .color = *color_target.clear_value };
	}
	else
	{
		color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
	}

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
							 .storeOp = vk::AttachmentStoreOp::eStore };
		if ( depth_target.clear_value )
		{
			depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
			depth_attachment.clearValue = { .depthStencil = { .depth = ( *depth_target.clear_value )[ 0 ] } };
		}
		else
		{
			depth_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		}
		if ( depth_target.resolve_target._view )
		{
			depth_attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
			depth_attachment.resolveImageView = depth_target.resolve_target._view;
			depth_attachment.resolveImageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		}
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

void renderer::CommandBuffer::bind_pipeline( const Pipeline& pipeline, const BindlessManagerBase& bindless_manager )
{
	const auto bind_point = pipeline.get_type() == Pipeline::Type::Compute ? vk::PipelineBindPoint::eCompute
																		   : vk::PipelineBindPoint::eGraphics;
	_cmd_buffer.bindPipeline( bind_point, pipeline._pipeline );
	const auto sets = bindless_manager.get_sets();
	for ( int i = 0; i < sets.size(); ++i )
	{
		_cmd_buffer.bindDescriptorSets( bind_point, pipeline._layout, i, sets[ i ], {} );
	}
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

void renderer::CommandBuffer::draw_indexed_indirect( const Buffer& buffer,
													 size_t offset,
													 const Buffer& count_buffer,
													 size_t count_offset,
													 uint32_t max_draws,
													 uint32_t stride )
{
	assert( offset < buffer.get_size() );
	assert( count_offset < count_buffer.get_size() );
	_cmd_buffer.drawIndexedIndirectCount( buffer.get_buffer(), offset, count_buffer.get_buffer(), count_offset, max_draws, stride );
}

void renderer::CommandBuffer::draw_mesh_tasks( uint32_t x, uint32_t y, uint32_t z )
{
	_cmd_buffer.drawMeshTasksEXT( x, y, z );
}

void renderer::CommandBuffer::draw_mesh_tasks_indirect( const Buffer& buffer,
														size_t offset,
														const Buffer& count_buffer,
														size_t count_offset,
														uint32_t max_draws,
														uint32_t stride )
{
	assert( offset < buffer.get_size() );
	assert( count_offset < count_buffer.get_size() );
	_cmd_buffer.drawMeshTasksIndirectCountEXT( buffer.get_buffer(), offset, count_buffer.get_buffer(), count_offset, max_draws, stride );
}

void renderer::CommandBuffer::dispatch( uint32_t x, uint32_t y, uint32_t z )
{
	_cmd_buffer.dispatch( x, y, z );
}

void renderer::CommandBuffer::reset_query( TimestampQuery query, uint32_t first, uint32_t count )
{
	_cmd_buffer.resetQueryPool( query, first, count );
}

void renderer::CommandBuffer::write_timestamp( TimestampQuery query, uint32_t index )
{
	_cmd_buffer.writeTimestamp( vk::PipelineStageFlagBits::eAllGraphics, query, index );
}

void renderer::CommandBuffer::reset_query( StatisticsQuery query )
{
	_cmd_buffer.resetQueryPool( query, 0, 1 );
}

void renderer::CommandBuffer::begin_query( StatisticsQuery query )
{
	_cmd_buffer.beginQuery( query, 0 );
}

void renderer::CommandBuffer::end_query( StatisticsQuery query )
{
	_cmd_buffer.endQuery( query, 0 );
}

void renderer::CommandBuffer::push_constants( const Pipeline& pipeline, const void* data, std::size_t size )
{
	assert( pipeline._desc.push_constants_size == size );
	_cmd_buffer.getDispatcher()->vkCmdPushConstants( *_cmd_buffer,
													 pipeline._layout,
													 static_cast<VkShaderStageFlags>( pipeline._used_stages ),
													 0,
													 static_cast<uint32_t>( size ),
													 data );
}
