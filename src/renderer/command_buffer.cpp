#include "command_buffer.h"

#include <cassert>
#include <renderer/bindless.h>
#include <renderer/buffer.h>
#include <renderer/details/profiler.h>
#include <renderer/device.h>
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

namespace renderer
{
	struct BarrierScope
	{
		vk::PipelineStageFlags2 stages;
		vk::AccessFlags2 access;
	};

	struct StageInfo
	{
		Stage stage;
		vk::PipelineStageFlags2 vk_stages;
		vk::AccessFlags2 writes;
		vk::AccessFlags2 reads;
	};

	constexpr StageInfo stage_infos[] = {
		{ Stage::INDIRECT, vk::PipelineStageFlagBits2::eDrawIndirect, { }, vk::AccessFlagBits2::eIndirectCommandRead },
		{ Stage::INDEX, vk::PipelineStageFlagBits2::eIndexInput, { }, vk::AccessFlagBits2::eIndexRead },
		{ Stage::VERTEX,
		  vk::PipelineStageFlagBits2::ePreRasterizationShaders,
		  vk::AccessFlagBits2::eShaderWrite,
		  vk::AccessFlagBits2::eShaderRead },
		{ Stage::FRAGMENT,
		  vk::PipelineStageFlagBits2::eFragmentShader,
		  vk::AccessFlagBits2::eShaderWrite,
		  vk::AccessFlagBits2::eShaderRead },
		{ Stage::DEPTH,
		  vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
		  vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
		  vk::AccessFlagBits2::eDepthStencilAttachmentRead },
		{ Stage::COLOR,
		  vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		  vk::AccessFlagBits2::eColorAttachmentWrite,
		  vk::AccessFlagBits2::eColorAttachmentRead },
		{ Stage::COMPUTE, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, vk::AccessFlagBits2::eShaderRead },
		{ Stage::TRANSFER,
		  vk::PipelineStageFlagBits2::eAllTransfer,
		  vk::AccessFlagBits2::eTransferWrite,
		  vk::AccessFlagBits2::eTransferRead },
		{ Stage::HOST, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite, vk::AccessFlagBits2::eHostRead },
	};

	// Reads never need to be made available, a write-after-read only needs the execution dependency
	BarrierScope producer_scope( Stage stages )
	{
		BarrierScope scope;
		for ( const auto& info : stage_infos )
		{
			if ( ( stages & info.stage ) != Stage::NONE )
			{
				scope.stages |= info.vk_stages;
				scope.access |= info.writes;
			}
		}
		return scope;
	}

	BarrierScope consumer_scope( Stage stages )
	{
		BarrierScope scope;
		for ( const auto& info : stage_infos )
		{
			if ( ( stages & info.stage ) != Stage::NONE )
			{
				scope.stages |= info.vk_stages;
				scope.access |= info.writes | info.reads;
			}
		}
		return scope;
	}

	// The acquire semaphore waits at color attachment output, and the submit semaphore signals after it
	constexpr BarrierScope present_scope { vk::PipelineStageFlagBits2::eColorAttachmentOutput, { } };
}

void renderer::CommandBuffer::transition_texture( const Texture& tex, Texture::Layout from, Texture::Layout to )
{
	assert( to != Texture::Layout::UNDEFINED );
	assert( ( from == Texture::Layout::GENERAL ) != ( to == Texture::Layout::GENERAL ) );

	BarrierScope src;
	BarrierScope dst;
	if ( from == Texture::Layout::UNDEFINED )
	{
		dst = consumer_scope( Stage::ALL );
	}
	else if ( from == Texture::Layout::PRESENT_SRC )
	{
		// Waiting for the acquire on all stages would stall the work that doesn't touch the swapchain image
		src = present_scope;
		dst = consumer_scope( Stage::COLOR | Stage::TRANSFER );
	}
	else
	{
		src = producer_scope( Stage::ALL );
		dst = present_scope;
	}

	// GENERAL is as fast as the dedicated layouts with VK_KHR_unified_image_layouts. Without it, some older GPUs lose
	// a bit of compression (eg: MSAA color on AMD before RDNA3), which we accept to avoid tracking layouts.
	// Leaving PRESENT_SRC goes from UNDEFINED, which also covers swapchain images that were never presented.
	const vk::ImageMemoryBarrier2 barrier {
		.srcStageMask = src.stages,
		.srcAccessMask = src.access,
		.dstStageMask = dst.stages,
		.dstAccessMask = dst.access,
		.oldLayout = from == Texture::Layout::GENERAL ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
		.newLayout = static_cast<vk::ImageLayout>( to ),
		.image = tex._image,
		.subresourceRange = { .aspectMask = Texture::is_depth_format( tex.get_format() ) ? vk::ImageAspectFlagBits::eDepth
																						 : vk::ImageAspectFlagBits::eColor,
							  .levelCount = VK_REMAINING_MIP_LEVELS,
							  .layerCount = VK_REMAINING_ARRAY_LAYERS }
	};
	_cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier } );
}

void renderer::CommandBuffer::barrier( Stage producers, Stage consumers )
{
	const auto src = producer_scope( producers );
	const auto dst = consumer_scope( consumers );
	const vk::MemoryBarrier2 barrier { .srcStageMask = src.stages,
									   .srcAccessMask = src.access,
									   .dstStageMask = dst.stages,
									   .dstAccessMask = dst.access };
	_cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .memoryBarrierCount = 1, .pMemoryBarriers = &barrier } );
}

void renderer::CommandBuffer::blit_texture( const Texture& src, const Texture& dst, int src_mip, int dst_mip, bool linear_filter )
{
	const auto src_size = src.get_extent( src_mip );
	const auto dst_size = dst.get_extent( dst_mip );
	const vk::ImageBlit2 blit_region {
		.srcSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = static_cast<uint32_t>( src_mip ), .layerCount = 1 },
		.srcOffsets = { { vk::Offset3D { }, vk::Offset3D( src_size.width, src_size.height, 1 ) } },
		.dstSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = static_cast<uint32_t>( dst_mip ), .layerCount = 1 },
		.dstOffsets = { { vk::Offset3D { }, vk::Offset3D( dst_size.width, dst_size.height, 1 ) } }
	};

	const vk::BlitImageInfo2 blit_info { .srcImage = src._image,
										 .srcImageLayout = vk::ImageLayout::eGeneral,
										 .dstImage = dst._image,
										 .dstImageLayout = vk::ImageLayout::eGeneral,
										 .regionCount = 1,
										 .pRegions = &blit_region,
										 .filter = linear_filter ? vk::Filter::eLinear : vk::Filter::eNearest };

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

void renderer::CommandBuffer::copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex, int mip )
{
	[[maybe_unused]] const auto block_size = Texture::get_texel_block_size( tex.get_format() );
	assert( block_size != 0 && offset % block_size == 0 );
	assert( mip < tex.get_mips() );
	_cmd_buffer.copyBufferToImage(
		buffer._buffer,
		tex._image,
		vk::ImageLayout::eGeneral,
		vk::BufferImageCopy { .bufferOffset = offset,
							  .imageSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor,
													.mipLevel = static_cast<uint32_t>( mip ),
													.layerCount = 1 },
							  .imageExtent = vk::Extent3D { .width = std::max( tex._desc.extent.width >> mip, 1u ),
															.height = std::max( tex._desc.extent.height >> mip, 1u ),
															.depth = 1 } } );
}

void renderer::CommandBuffer::fill_buffer( const Buffer& buffer, size_t offset, size_t size, uint32_t value )
{
	assert( offset + size <= buffer.get_size() );
	_cmd_buffer.fillBuffer( buffer.get_buffer(), offset, size, value );
}

void renderer::CommandBuffer::begin_rendering( RenderInfo info )
{
	vk::RenderingAttachmentInfo color_attachment;
	vk::RenderingAttachmentInfo depth_attachment;
	vk::RenderingInfo renderInfo { .renderArea = vk::Rect2D { .extent = info.extent },
								   .layerCount = 1,
								   .pDepthAttachment = info.depth_target.target._view ? &depth_attachment : nullptr };

	if ( info.color_target.target._view )
	{
		color_attachment = { .imageView = info.color_target.target._view,
							 .imageLayout = vk::ImageLayout::eGeneral,
							 .storeOp = vk::AttachmentStoreOp::eStore };
		if ( info.color_target.clear_value )
		{
			color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
			color_attachment.clearValue = { .color = vk::ClearColorValue( *info.color_target.clear_value ) };
		}
		else
		{
			color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		}

		if ( info.color_target.resolve_target._view )
		{
			color_attachment.resolveMode = static_cast<vk::ResolveModeFlagBits>( info.color_target.resolve_mode );
			color_attachment.resolveImageView = info.color_target.resolve_target._view;
			color_attachment.resolveImageLayout = vk::ImageLayout::eGeneral;
		}

		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &color_attachment;
		if ( renderInfo.renderArea.extent.height == 0u && renderInfo.renderArea.extent.width == 0u )
		{
			renderInfo.renderArea.extent = info.color_target.target.get_extent();
		}
	}

	if ( info.depth_target.target._view )
	{
		depth_attachment = { .imageView = info.depth_target.target._view,
							 .imageLayout = vk::ImageLayout::eGeneral,
							 .storeOp = vk::AttachmentStoreOp::eStore };
		if ( info.depth_target.clear_value )
		{
			depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
			depth_attachment.clearValue = { .depthStencil = { .depth = ( *info.depth_target.clear_value )[ 0 ] } };
		}
		else
		{
			depth_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		}
		if ( info.depth_target.resolve_target._view )
		{
			depth_attachment.resolveMode = static_cast<vk::ResolveModeFlagBits>( info.depth_target.resolve_mode );
			depth_attachment.resolveImageView = info.depth_target.resolve_target._view;
			depth_attachment.resolveImageLayout = vk::ImageLayout::eGeneral;
		}

		renderInfo.pDepthAttachment = &depth_attachment;
		if ( renderInfo.renderArea.extent.height == 0u && renderInfo.renderArea.extent.width == 0u )
		{
			renderInfo.renderArea.extent = info.depth_target.target.get_extent();
		}
	}

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
	for ( uint32_t i = 0; i < sets.size(); ++i )
	{
		_cmd_buffer.bindDescriptorSets( bind_point, pipeline._layout, i, sets[ i ], { } );
	}
}

void renderer::CommandBuffer::set_dynamic_state( const Pipeline::DynamicState& state )
{
	set_viewport( state.viewport );
	set_scissor( state.scissor.width == 0 && state.scissor.height == 0 ? state.viewport : state.scissor );
	set_cull_mode( state.cull_mode );
	set_depth_test( state.depth_test );
	set_depth_write( state.depth_write );
	set_depth_bias( state.depth_bias_constant, state.depth_bias_slope, state.depth_bias_clamp );
}

void renderer::CommandBuffer::set_scissor( Extent2D extent )
{
	const vk::Rect2D scissor { .extent = extent };
	_cmd_buffer.setScissor( 0, scissor );
}

void renderer::CommandBuffer::set_viewport( Extent2D extent )
{
	// As documented in the API we flip Y axis to mirror the most common convention rather than indulging Vulkan's default
	const vk::Viewport viewport { .x = 0.f,
								  .y = static_cast<float>( extent.height ),
								  .width = static_cast<float>( extent.width ),
								  .height = -static_cast<float>( extent.height ),
								  .minDepth = 0.f,
								  .maxDepth = 1.f };

	_cmd_buffer.setViewport( 0, viewport );
}

void renderer::CommandBuffer::set_cull_mode( Pipeline::CullMode mode )
{
	_cmd_buffer.setCullMode( static_cast<vk::CullModeFlagBits>( mode ) );
}

void renderer::CommandBuffer::set_depth_test( bool test )
{
	_cmd_buffer.setDepthTestEnable( test );
}

void renderer::CommandBuffer::set_depth_write( bool write )
{
	_cmd_buffer.setDepthWriteEnable( write );
}

void renderer::CommandBuffer::set_depth_bias( float constant_factor, float slope_factor, float clamp )
{
	const bool enable = constant_factor != 0.f || slope_factor != 0.f;
	_cmd_buffer.setDepthBiasEnable( enable );
	_cmd_buffer.setDepthBias( constant_factor, clamp, slope_factor );
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

void renderer::CommandBuffer::draw_indexed_indirect( const Buffer& buffer, size_t offset, uint32_t count, uint32_t stride )
{
	assert( offset < buffer.get_size() );
	_cmd_buffer.drawIndexedIndirect( buffer.get_buffer(), offset, count, stride );
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
	if ( query )
	{
		_cmd_buffer.resetQueryPool( query, first, count );
	}
}

void renderer::CommandBuffer::write_timestamp( TimestampQuery query, uint32_t index )
{
	if ( query )
	{
		_cmd_buffer.writeTimestamp( vk::PipelineStageFlagBits::eAllCommands, query, index );
	}
}

void renderer::CommandBuffer::reset_query( StatisticsQuery query )
{
	if ( query )
	{
		_cmd_buffer.resetQueryPool( query, 0, 1 );
	}
}

void renderer::CommandBuffer::begin_query( StatisticsQuery query )
{
	if ( query )
	{
		_cmd_buffer.beginQuery( query, 0 );
	}
}

void renderer::CommandBuffer::end_query( StatisticsQuery query )
{
	if ( query )
	{
		_cmd_buffer.endQuery( query, 0 );
	}
}

renderer::CommandBuffer::DebugLabelScope renderer::CommandBuffer::debug_label( const char* name, const std::array<float, 4>& color )
{
	if ( !_debug_labels_support )
	{
		return DebugLabelScope( nullptr );
	}

	_cmd_buffer.beginDebugUtilsLabelEXT( vk::DebugUtilsLabelEXT { .pLabelName = name, .color = color } );
	return DebugLabelScope( this );
}

renderer::CommandBuffer::DebugLabelScope::~DebugLabelScope()
{
	if ( _cmd )
	{
		_cmd->_cmd_buffer.endDebugUtilsLabelEXT();
	}
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
