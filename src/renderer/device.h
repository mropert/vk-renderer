#pragma once

#include <array>
#include <initializer_list>
#include <queue>
#include <renderer/buffer.h>
#include <renderer/common.h>
#include <renderer/pipeline.h>
#include <renderer/sampler.h>
#include <renderer/texture.h>

namespace renderer
{
	class BindlessManager;
	class CommandBuffer;
	class Device;

	namespace raii
	{
		struct CommandBufferDeleter
		{
			Device* device;
			void* optick_previous;
			void operator()( renderer::CommandBuffer* cmd ) const;
		};
		using CommandBuffer = std::unique_ptr<renderer::CommandBuffer, CommandBufferDeleter>;

		class Pipeline;
		class ShaderCode;
	}

	class Device
	{
	public:
		explicit Device( const char* appname );
		~Device();

		void wait_idle();

		raii::CommandBuffer grab_command_buffer();
		void release_command_buffer( CommandBuffer* buffer, void* optick_previous );

		raii::Texture create_texture( Texture::Format format, Texture::Usage usage, Extent2D extent, int samples = 1 );
		raii::TextureView create_texture_view( const Texture& texture, TextureView::Aspect aspect );

		raii::Sampler create_sampler( Sampler::Filter filter );

		raii::Buffer create_buffer( Buffer::Usage usage, std::size_t size, bool upload = false );

		raii::Pipeline create_pipeline( const Pipeline::Desc& desc,
										const raii::ShaderCode& vertex_code,
										const raii::ShaderCode& fragment_code,
										const BindlessManager& bindless_manager );

		raii::Fence create_fence( bool signaled = false );
		void wait_for_fences( std::initializer_list<Fence> fences, uint64_t timeout );

		void submit( CommandBuffer& buffer, Fence signal_fence );

		const Extent2D& get_extent() const { return _extent; }

		// Queue resource for deletion once MAX_FRAMES_IN_FLIGHT have been submitted for presentation
		void queue_deletion( raii::Pipeline pipeline );

	private:
		void notify_present();

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
		std::array<std::vector<raii::Pipeline>, MAX_FRAMES_IN_FLIGHT> _delete_queue;
		uint32_t _delete_index = 0;

		friend class BindlessManager;
		friend class Swapchain;
	};

	namespace raii
	{
		inline void CommandBufferDeleter::operator()( renderer::CommandBuffer* cmd ) const
		{
			device->release_command_buffer( cmd, optick_previous );
		}
	}
}
