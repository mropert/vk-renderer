#pragma once

#include <array>
#include <initializer_list>
#include <queue>
#include <renderer/buffer.h>
#include <renderer/common.h>
#include <renderer/pipeline.h>
#include <renderer/sampler.h>
#include <renderer/shader.h>
#include <renderer/texture.h>
#include <span>

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
			void operator()( renderer::CommandBuffer* cmd ) const;
		};
		using CommandBuffer = std::unique_ptr<renderer::CommandBuffer, CommandBufferDeleter>;

		class Pipeline;
	}

	class Device
	{
	public:
		explicit Device( const char* appname );
		~Device();

		void wait_idle();

		raii::CommandBuffer grab_command_buffer();
		void release_command_buffer( CommandBuffer* buffer );

		raii::Texture create_texture( Texture::Format format, Texture::Usage usage, Extent2D extent, int samples = 1 );
		raii::TextureView create_texture_view( const Texture& texture, TextureView::Aspect aspect );

		raii::Sampler create_sampler( Sampler::Filter filter );

		raii::Buffer create_buffer( Buffer::Usage usage, std::size_t size, bool upload = false );

		raii::Pipeline
		create_pipeline( const Pipeline::Desc& desc, std::span<const ShaderCode> shaders, const BindlessManager& bindless_manager );

		raii::Fence create_fence( bool signaled = false );
		void wait_for_fences( std::span<const Fence> fences, uint64_t timeout );
		void wait_for_fences( std::initializer_list<Fence> fences, uint64_t timeout )
		{
			wait_for_fences( std::span( begin( fences ), fences.size() ), timeout );
		}

		void submit( CommandBuffer& buffer, Fence signal_fence );

		raii::QueryPool create_query_pool( uint32_t size );
		void get_query_pool_results( QueryPool pool, uint32_t first_index, std::span<uint64_t> results );
		float get_timestamp_period() const;

		const Extent2D& get_extent() const { return _extent; }

		void set_relative_mouse_mode( bool enabled );

		// Queue resource for deletion once MAX_FRAMES_IN_FLIGHT have been submitted for presentation
		void queue_deletion( raii::Pipeline pipeline );

		// Renderer internals, can be queried if needed to interact with a 3rd party (eg: imgui)
		struct Internals
		{
			uint32_t api_version;
			SDL_Window* window;
			VkInstance instance;
			VkPhysicalDevice physical_device;
			VkDevice device;
			uint32_t queue_family;
			VkQueue queue;
		};
		Internals get_internals() const;

		struct Properties
		{
			std::string name;
			std::size_t host_memory_size;
			std::size_t device_memory_size;
			std::size_t transfer_memory_size;
			bool mesh_shader_support = false;
			uint32_t max_mesh_shader_groups = 0;
			std::array<uint32_t, 3> max_mesh_shader_group_size;
		};

		const Properties& get_properties() const { return _properties; }

	private:
		void notify_present();
		void set_properties();

		sdl::raii::Window _window;
		Extent2D _extent;
		vk::raii::Context _context;
		vk::raii::Instance _instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT _debug_util = nullptr;
		vk::raii::SurfaceKHR _surface = nullptr;
		vk::raii::PhysicalDevice _physical_device = nullptr;
		Properties _properties;
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
			device->release_command_buffer( cmd );
		}
	}
}
