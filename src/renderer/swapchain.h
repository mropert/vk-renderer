#pragma once

#include <renderer/common.h>

namespace renderer
{
	class Device;

	class Swapchain
	{
	public:
		explicit Swapchain( Device& device, VkFormat format );

//	private:
		vk::raii::SwapchainKHR _swapchain = nullptr;
		std::vector<vk::Image> _images;
	};
}
