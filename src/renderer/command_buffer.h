#pragma once

#include <array>
#include <optional>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class BindlessManagerBase;
	class Buffer;
	class Pipeline;
	class TextureView;

	// Simplified stage + access fold for most common barriers, inspired by D3D12_RESOURCE_STATE
	enum class ResourceState
	{
		UNDEFINED,
		INDIRECT_ARGUMENT,
		INDEX_READ,
		VERTEX_SHADER_READ,	// Includes geometry and mesh shaders
		FRAGMENT_SHADER_READ,
		COMPUTE_SHADER_READ,
		SHADER_READ,
		STORAGE_READ,
		ANY_BUFFER_READ, // Any buffer read from shaders, indices and indirect
		STORAGE_WRITE,
		STORAGE_READ_WRITE,
		COLOR_ATTACHMENT,
		DEPTH_ATTACHMENT,
		DEPTH_READ,
		TRANSFER_SRC,
		TRANSFER_DST,
		PRESENT
	};

	struct RenderAttachment
	{
		TextureView target;
		TextureView resolve_target;
		std::optional<std::array<float, 4>> clear_value;
	};

	struct RenderInfo
	{
		RenderAttachment color_target;
		RenderAttachment depth_target;
		Extent2D extent;	// Defaults to color_target extent if left blank
	};

	class CommandBuffer
	{
	public:
		void begin();
		void end();
		void reset();

		void texture_barrier( const Texture& tex, ResourceState src, ResourceState dst, int mip_level = -1 );
		void blit_texture( const Texture& src, const Texture& dst );

		void copy_buffer( const Buffer& src, std::size_t offset, std::size_t size, const Buffer& dest, std::size_t dest_offset = 0 );
		void copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex );
		void fill_buffer( const Buffer& buffer, size_t offset, size_t size, uint32_t value );
		void buffer_barrier( const Buffer& buffer, ResourceState src, ResourceState dst );

		void memory_barrier( ResourceState src, ResourceState dst );

		void begin_rendering( RenderInfo info );
		void end_rendering();

		void bind_pipeline( const Pipeline& pipeline, const BindlessManagerBase& bindless_manager );

		void set_scissor( Extent2D extent );
		// Will flip Y axis on the NDC behind the scene to be consistent with every other rendering API in town
		void set_viewport( Extent2D extent );

		template <typename T>
		void push_constants( const Pipeline& pipeline, const T& data )
		{
			push_constants( pipeline, &data, sizeof( T ) );
		}

		void bind_index_buffer( const Buffer& index_buffer );

		void draw( uint32_t count );
		void draw_indexed( uint32_t count, uint32_t instance_count = 1, uint32_t first_index = 0, uint32_t first_instance = 0 );
		void draw_indexed_indirect( const Buffer& buffer, size_t offset, uint32_t count, uint32_t stride );
		void draw_indexed_indirect( const Buffer& buffer,
									size_t offset,
									const Buffer& count_buffer,
									size_t count_offset,
									uint32_t max_draws,
									uint32_t stride );
		void draw_mesh_tasks( uint32_t x, uint32_t y, uint32_t z );
		void draw_mesh_tasks_indirect( const Buffer& buffer,
									   size_t offset,
									   const Buffer& count_buffer,
									   size_t count_offset,
									   uint32_t max_draws,
									   uint32_t stride );

		void dispatch( uint32_t x, uint32_t y, uint32_t z );

		// Statistic queries
		// No-op if query objects are null/empty to simplify handling devices without support for statistics (like MoltenVK)
		void reset_query( TimestampQuery query, uint32_t first, uint32_t count );
		void write_timestamp( TimestampQuery query, uint32_t index );
		void reset_query( StatisticsQuery query );
		void begin_query( StatisticsQuery query );
		void end_query( StatisticsQuery query );

		// Get the underlying renderer buffer, for integration with 3rd party (eg: imgui)
		VkCommandBuffer get_impl() const { return *_cmd_buffer; }

	private:
		explicit CommandBuffer( vk::raii::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

		// Lower level barriers aren't exposed for now, until we find a use case for them
		void texture_barrier( const Texture& tex,
							  Texture::Layout src_layout,
							  Texture::Layout dst_layout,
							  vk::PipelineStageFlags2 src_stage,
							  vk::PipelineStageFlags2 dst_stage,
							  vk::AccessFlags2 src_access,
							  vk::AccessFlags2 dst_access,
							  int mip_level = -1 );

		void buffer_barrier( const Buffer& buffer,
							 vk::PipelineStageFlags2 src_stage,
							 vk::PipelineStageFlags2 dst_stage,
							 vk::AccessFlags2 src_access,
							 vk::AccessFlags2 dst_access );

		void push_constants( const Pipeline& pipeline, const void* data, std::size_t size );

		vk::raii::CommandBuffer _cmd_buffer;
		void* _optick_previous = nullptr;

		friend class Device;
		friend class Swapchain;
		friend class Texture;
	};
}
