#include "device.h"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <print>
#include <renderer/command_buffer.h>

renderer::Device::Device( const char* appname )
{
	if ( !SDL_Init( SDL_INIT_VIDEO ) )
	{
		throw renderer_error( SDL_GetError() );
	}

	const auto display = SDL_GetPrimaryDisplay();
	const auto mode = SDL_GetCurrentDisplayMode( display );

	_extent = { .width = static_cast<uint32_t>( mode->w ), .height = static_cast<uint32_t>( mode->h ) };
	_window.reset( SDL_CreateWindow( appname, _extent.width, _extent.height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_VULKAN ) );
	if ( !_window )
	{
		throw renderer_error( SDL_GetError() );
	}

	Uint32 ext_count = 0;
	const auto exts = SDL_Vulkan_GetInstanceExtensions( &ext_count );

	const auto vk_instance_result = vkb::InstanceBuilder()
										.set_app_name( "Brutus-ng Vulkan" )
										.enable_validation_layers( true )
										.use_default_debug_messenger()
										.require_api_version( 1, 3, 0 )
										.enable_extensions( ext_count, exts )
										.build();

	if ( !vk_instance_result )
	{
		throw renderer_error( vk_instance_result.error(), vk_instance_result.vk_result() );
	}
	_instance = { _context, vk_instance_result.value().instance };
	_debug_util = { _instance, vk_instance_result.value().debug_messenger };

	VkSurfaceKHR surface {};
	if ( !SDL_Vulkan_CreateSurface( _window.get(), *_instance, nullptr, &surface ) )
	{
		throw renderer_error( SDL_GetError() );
	}

	_surface = vk::raii::SurfaceKHR( _instance, surface );

	const VkPhysicalDeviceVulkan13Features features13 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
														.synchronization2 = true,
														.dynamicRendering = true };

	const VkPhysicalDeviceVulkan12Features features12 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
														.descriptorIndexing = true,
														.bufferDeviceAddress = true };

	const auto physical_device_ret = vkb::PhysicalDeviceSelector( vk_instance_result.value() )
										 .set_minimum_version( 1, 3 )
										 .set_required_features_13( features13 )
										 .set_required_features_12( features12 )
										 .set_surface( *_surface )
										 .select();

	if ( !physical_device_ret )
	{
		throw renderer_error( physical_device_ret.error(), physical_device_ret.vk_result() );
	}

	_physical_device = { _instance, physical_device_ret.value() };

	const auto props = _physical_device.getProperties2();
	std::println( "Using hardware device: {}", props.properties.deviceName.data() );

	const auto device_ret = vkb::DeviceBuilder( physical_device_ret.value() ).build();
	if ( !device_ret )
	{
		throw renderer_error( device_ret.error(), device_ret.vk_result() );
	}

	_device = { _physical_device, device_ret.value() };
	_gfx_queue_family_index = device_ret.value().get_queue_index( vkb::QueueType::graphics ).value();
	_present_queue_family_index = device_ret.value().get_queue_index( vkb::QueueType::present ).value();
	_gfx_queue = _device.getQueue( _gfx_queue_family_index, 0 );

	VmaAllocator allocator {};
	const VmaAllocatorCreateInfo allocatorInfo = {
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = *_physical_device,
		.device = *_device,
		.instance = *_instance,
	};
	const auto ret = vmaCreateAllocator( &allocatorInfo, &allocator );
	if ( ret )
	{
		throw renderer_error( "Failed to create vma allocator", ret );
	}
	_allocator.reset( allocator );

	_command_pool = _device.createCommandPool( vk::CommandPoolCreateInfo { .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
																		   .queueFamilyIndex = _gfx_queue_family_index } );

	auto buffers = _device.allocateCommandBuffers( vk::CommandBufferAllocateInfo { .commandPool = _command_pool,
																				   .level = vk::CommandBufferLevel::ePrimary,
																				   .commandBufferCount = MAX_FRAMES_IN_FLIGHT } );
	_command_buffers.reserve( buffers.size() );
	for ( auto& buffer : buffers )
	{
		_command_buffers.push_back( CommandBuffer( std::move( buffer ) ) );
		_available_command_buffers.push( &_command_buffers.back() );
	}
}

renderer::Device::~Device() = default;

renderer::raii::CommandBuffer renderer::Device::grab_command_buffer()
{
	raii::CommandBuffer cmd( _available_command_buffers.front(), raii::CommandBufferDeleter { this } );
	_available_command_buffers.pop();
	return cmd;
}

void renderer::Device::release_command_buffer( CommandBuffer* buffer )
{
	_available_command_buffers.push( buffer );
}

renderer::raii::Texture renderer::Device::create_texture( Texture::Format format, Texture::Usage usage, Extent2D extent, int samples )
{
	const VkImageCreateInfo info { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								   .imageType = VK_IMAGE_TYPE_2D,
								   .format = static_cast<VkFormat>( format ),
								   .extent = { .width = extent.width, .height = extent.height, .depth = 1 },
								   .mipLevels = 1,
								   .arrayLayers = 1,
								   .samples = static_cast<VkSampleCountFlagBits>( samples ),
								   .tiling = VK_IMAGE_TILING_OPTIMAL,
								   .usage = static_cast<VkImageUsageFlags>( usage ) };

	const VmaAllocationCreateInfo create_info { .usage = VMA_MEMORY_USAGE_GPU_ONLY, .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT };

	VkImage image {};
	VmaAllocation allocation {};
	VmaAllocationInfo allocation_info {};
	auto ret = vmaCreateImage( _allocator.get(), &info, &create_info, &image, &allocation, &allocation_info );
	if ( ret )
	{
		throw renderer_error( "Failed to create image", ret );
	}

	return raii::Texture( renderer::Texture { image, format, usage, extent, samples },
						  vma::raii::Allocation { _allocator.get(), allocation, allocation_info } );
}

renderer::raii::TextureView renderer::Device::create_texture_view( const Texture& texture, TextureView::Aspect aspect )
{
	const vk::ImageViewCreateInfo image_view_info { .image = texture._image,
													.viewType = vk::ImageViewType::e2D,
													.format = static_cast<vk::Format>( texture._format ),
													.subresourceRange = { .aspectMask = static_cast<vk::ImageAspectFlagBits>( aspect ),
																		  .baseMipLevel = 0,
																		  .levelCount = 1,
																		  .baseArrayLayer = 0,
																		  .layerCount = 1 } };


	auto view = _device.createImageView( image_view_info );
	return raii::TextureView( std::move( view ) );
}
