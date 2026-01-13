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

	struct RenderAttachment
	{
		TextureView target;
		TextureView resolve_target;
		std::optional<std::array<float, 4>> clear_value;
	};

	class CommandBuffer
	{
	public:
		void begin();
		void end();
		void reset();

		void transition_texture( Texture& tex, Texture::Layout target );
		void blit_texture( const Texture& src, const Texture& dst );

		void copy_buffer( const Buffer& src, std::size_t offset, std::size_t size, const Buffer& dest, std::size_t dest_offset = 0 );
		void copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex );

		void begin_rendering( Extent2D extent, RenderAttachment color_target, RenderAttachment depth_target = {} );
		void end_rendering();

		void bind_pipeline( const Pipeline& pipeline, const BindlessManagerBase& bindless_manager );

		void set_scissor( Extent2D extent );
		void set_viewport( Extent2D extent );

		template <typename T>
		void push_constants( const Pipeline& pipeline, const T& data )
		{
			push_constants( pipeline, &data, sizeof( T ) );
		}

		void bind_index_buffer( const Buffer& index_buffer );

		void draw( uint32_t count );
		void draw_indexed( uint32_t count, uint32_t instance_count = 1, uint32_t first_index = 0, uint32_t first_instance = 0 );
		void draw_mesh_task( uint32_t x, uint32_t y, uint32_t z );

		void dispatch( uint32_t x, uint32_t y, uint32_t z );

		void reset_query_pool( QueryPool pool, uint32_t first, uint32_t count );
		void write_timestamp( QueryPool pool, uint32_t index );

		// Get the underlying renderer buffer, for integration with 3rd party (eg: imgui)
		VkCommandBuffer get_impl() const { return *_cmd_buffer; }

	private:
		explicit CommandBuffer( vk::raii::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

		void texture_barrier( Texture& tex,
							  Texture::Layout target,
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
