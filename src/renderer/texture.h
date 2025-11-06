#pragma once

#include <renderer/common.h>

namespace renderer
{
	class Texture
	{
	public:
		// private:
		explicit Texture( vma::raii::Image image );
		explicit Texture( VkImage image );

		vma::raii::Image _image;

		friend class Device;
		friend class Swapchain;
	};
}
