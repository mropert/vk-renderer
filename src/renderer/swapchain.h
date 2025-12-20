#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class CommandBuffer;
	class Device;

	class Swapchain
	{
	public:
		explicit Swapchain( Device& device, Texture::Format format, bool vsync = true );

		uint32_t get_frame_count() const { return _frame_count; }
		uint32_t get_image_count() const { return _images.size(); }

		std::tuple<uint32_t, Texture, TextureView> acquire();
		void submit( CommandBuffer& buffer );
		void present();

	private:
		struct FrameData
		{
			raii::Fence render_fence = nullptr;
			vk::raii::Semaphore render_semaphore = nullptr;
			vk::raii::Semaphore swapchain_semaphore = nullptr;
		};

		Device* _device;
		vk::raii::SwapchainKHR _swapchain = nullptr;
		std::vector<Texture> _images;
		std::vector<raii::TextureView> _image_views;
		std::array<FrameData, MAX_FRAMES_IN_FLIGHT> _frames_data;
		uint32_t _frame_count = 0;
		uint32_t _current_image = -1;
	};
}
