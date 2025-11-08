#pragma once

#include <renderer/common.h>

namespace renderer
{
	class CommandBuffer;

	class Texture
	{
	public:
		void transition( CommandBuffer& cmd, vk::ImageLayout from, vk::ImageLayout to );

		// private:
		explicit Texture( vk::Image image )
			: _image( image )
		{
		}

		vk::Image _image;

		friend class Device;
		friend class Swapchain;
	};

	class TextureView
	{
	public:
		// private:
		explicit TextureView( vk::ImageView view )
			: _view( std::move( view ) )
		{
		}

		vk::ImageView _view;

		friend class CommandBuffer;
		friend class Device;
	};
}
