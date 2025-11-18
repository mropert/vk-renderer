#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
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

		void begin_rendering( Extent2D extent, RenderAttachment color_target, RenderAttachment depth_target );
		void end_rendering();

		void transition_texture( Texture& tex, Texture::Layout target );
		void blit_texture( const Texture& src, const Texture& dst );

		void set_scissor( Extent2D extent );
		void set_viewport( Extent2D extent );

	private:
		explicit CommandBuffer( vk::raii::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

	public:
		vk::raii::CommandBuffer _cmd_buffer;

		friend class Device;
		friend class Swapchain;
		friend class Texture;
	};
}
