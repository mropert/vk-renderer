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
		explicit Texture( vma::raii::Image image );
		explicit Texture( VkImage image );

		vma::raii::Image _image;

		friend class Device;
		friend class Swapchain;
	};
}
