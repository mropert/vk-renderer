#pragma once

#include <renderer/common.h>

namespace renderer
{
	class Device
	{
	public:
		explicit Device( const char* appname );

//	private:
		const Extent2D& get_extent() const { return _extent; }

		sdl::raii::Window _window;
		vk::raii::Context _context;
		vk::raii::Instance _instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT _debug_util = nullptr;
		vk::raii::SurfaceKHR _surface = nullptr;
		vk::raii::PhysicalDevice _physical_device = nullptr;
		vk::raii::Device _device = nullptr;
		uint32_t _gfx_queue_family_index = 0;
		uint32_t _present_queue_family_index = 0;
		vk::raii::Queue _gfx_queue = nullptr;
		vma::raii::Allocator _allocator;
		Extent2D _extent;

		friend class Swapchain;
	};
}
