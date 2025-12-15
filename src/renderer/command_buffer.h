#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class BindlessManager;
	class Buffer;
	class Pipeline;
	class TextureView;

	struct RenderAttachment
	{
		TextureView target;
		TextureView resolve_target;
		std::array<float, 4> clear_value;
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

		void begin_rendering( Extent2D extent, RenderAttachment color_target, RenderAttachment depth_target );
		void end_rendering();

		void bind_pipeline( const Pipeline& pipeline, const BindlessManager& bindless_manager );

		void set_scissor( Extent2D extent );
		void set_viewport( Extent2D extent );

		template <typename T>
		void push_constants( const Pipeline& pipeline, const T& data )
		{
			push_constants( pipeline, &data, sizeof( T ) );
		}

		void draw( uint32_t count );
		void draw_indexed( const Buffer& index_buffer, uint32_t instance_count = 1 );

		// Get the underlying renderer buffer, for integration with 3rd party (eg: imgui)
		VkCommandBuffer get_impl() const { return *_cmd_buffer; }

	private:
		explicit CommandBuffer( vk::raii::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

		void push_constants( const Pipeline& pipeline, const void* data, std::size_t size );

		vk::raii::CommandBuffer _cmd_buffer;
		void* _optick_previous = nullptr;

		friend class Device;
		friend class Swapchain;
		friend class Texture;
	};
}
