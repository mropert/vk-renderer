#pragma once

#include <queue>
#include <renderer/buffer.h>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class CommandBuffer;
	class Device;

	namespace raii
	{
		struct CommandBufferDeleter
		{
			Device* device;
			void operator()( renderer::CommandBuffer* cmd ) const;
		};
		using CommandBuffer = std::unique_ptr<renderer::CommandBuffer, CommandBufferDeleter>;
	}

	class Device
	{
	public:
		explicit Device( const char* appname );
		~Device();

		raii::CommandBuffer grab_command_buffer();
		void release_command_buffer( CommandBuffer* buffer );

		raii::Texture create_texture( Texture::Format format, Texture::Usage usage, Extent2D extent, int samples = 1 );
		raii::TextureView create_texture_view( const Texture& texture, TextureView::Aspect aspect );

		raii::Buffer create_buffer( Buffer::Usage usage, std::size_t size, bool upload = false );

		//	private:
		const Extent2D& get_extent() const { return _extent; }

		sdl::raii::Window _window;
		Extent2D _extent;
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
		vk::raii::CommandPool _command_pool = nullptr;
		std::vector<CommandBuffer> _command_buffers;
		std::queue<CommandBuffer*> _available_command_buffers;

		friend class Swapchain;
	};

	namespace raii
	{
		inline void CommandBufferDeleter::operator()( renderer::CommandBuffer* cmd ) const
		{
			device->release_command_buffer( cmd );
		}
	}
}
