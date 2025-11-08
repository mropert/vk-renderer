#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class CommandBuffer;
	class Device;
	class Texture;

	class Swapchain
	{
	public:
		explicit Swapchain( Device& device, VkFormat format );

		uint32_t get_frame_count() const { return _frame_count; }

		std::tuple<uint32_t, Texture> acquire();
		void submit( CommandBuffer& buffer );
		void present();

		//	private:
		struct FrameData
		{
			vk::raii::Fence render_fence = nullptr;
			vk::raii::Semaphore render_semaphore = nullptr;
			vk::raii::Semaphore swapchain_semaphore = nullptr;
		};

		Device* _device;
		vk::raii::SwapchainKHR _swapchain = nullptr;
		std::vector<Texture> _images;
		std::array<FrameData, MAX_FRAMES_IN_FLIGHT> _frames_data;
		uint32_t _frame_count = 0;
		uint32_t _current_image = -1;
	};
}
