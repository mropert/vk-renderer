#include "device.h"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <print>
#include <renderer/bindless.h>
#include <renderer/command_buffer.h>
#include <renderer/pipeline.h>
#include <renderer/shader.h>

renderer::Device::Device( const char* appname )
{
	if ( !SDL_Init( SDL_INIT_VIDEO ) )
	{
		throw Error( SDL_GetError() );
	}

	const auto display = SDL_GetPrimaryDisplay();
	const auto mode = SDL_GetCurrentDisplayMode( display );

	_extent = { .width = static_cast<uint32_t>( mode->w ), .height = static_cast<uint32_t>( mode->h ) };
	_window.reset( SDL_CreateWindow( appname, _extent.width, _extent.height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_VULKAN ) );
	if ( !_window )
	{
		throw Error( SDL_GetError() );
	}

	Uint32 ext_count = 0;
	const auto exts = SDL_Vulkan_GetInstanceExtensions( &ext_count );

	const auto vk_instance_result = vkb::InstanceBuilder()
										.set_app_name( appname )
#if _DEBUG
										.enable_validation_layers( true )
#endif
										.use_default_debug_messenger()
										.require_api_version( 1, 3, 0 )
										.enable_extensions( ext_count, exts )
										.build();

	if ( !vk_instance_result )
	{
		throw Error( vk_instance_result.error(), vk_instance_result.vk_result() );
	}
	_instance = { _context, vk_instance_result.value().instance };
	_debug_util = { _instance, vk_instance_result.value().debug_messenger };

	VkSurfaceKHR surface {};
	if ( !SDL_Vulkan_CreateSurface( _window.get(), *_instance, nullptr, &surface ) )
	{
		throw Error( SDL_GetError() );
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
		throw Error( physical_device_ret.error(), physical_device_ret.vk_result() );
	}

	_physical_device = { _instance, physical_device_ret.value() };

	const auto props = _physical_device.getProperties2();
	std::println( "Using hardware device: {}", props.properties.deviceName.data() );

	const auto device_ret = vkb::DeviceBuilder( physical_device_ret.value() ).build();
	if ( !device_ret )
	{
		throw Error( device_ret.error(), device_ret.vk_result() );
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
		throw Error( "Failed to create vma allocator", ret );
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

void renderer::Device::wait_idle()
{
	_device.waitIdle();
}

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
		throw Error( "Failed to create image", ret );
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

renderer::raii::Sampler renderer::Device::create_sampler( Sampler::Filter filter )
{
	auto sampler = _device.createSampler(
		vk::SamplerCreateInfo { .magFilter = static_cast<vk::Filter>( filter ), .minFilter = static_cast<vk::Filter>( filter ) } );
	return raii::Sampler( std::move( sampler ) );
}

renderer::raii::Buffer renderer::Device::create_buffer( Buffer::Usage usage, std::size_t size, bool upload )
{
	const VkBufferCreateInfo buffer_Info { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
										   .size = size,
										   .usage = static_cast<VkBufferUsageFlags>( usage ) };
	const VmaAllocationCreateInfo vma_alloc_info = { .flags = upload ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0u,
													 .usage = upload ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY };

	VkBuffer buffer {};
	VmaAllocation allocation {};
	VmaAllocationInfo allocation_info {};
	const auto ret = vmaCreateBuffer( _allocator.get(), &buffer_Info, &vma_alloc_info, &buffer, &allocation, &allocation_info );
	if ( ret )
	{
		throw Error( "Failed to create buffer", ret );
	}

	vk::DeviceAddress address = 0;
	if ( ( usage & Buffer::Usage::SHADER_DEVICE_ADDRESS ) == Buffer::Usage::SHADER_DEVICE_ADDRESS )
	{
		address = _device.getBufferAddress( vk::BufferDeviceAddressInfo { .buffer = buffer } );
	}

	return raii::Buffer( renderer::Buffer { buffer, address, allocation_info.pMappedData, size, usage },
						 vma::raii::Allocation { _allocator.get(), allocation, allocation_info } );
}

renderer::raii::Pipeline renderer::Device::create_pipeline( const Pipeline::Desc& desc,
															const raii::ShaderCode& vertex_code,
															const raii::ShaderCode& fragment_code,
															const BindlessManager& bindless_manager )
{
	const auto vertex_shader = _device.createShaderModule(
		{ .codeSize = vertex_code.get_size() * sizeof( uint32_t ), .pCode = vertex_code.get_data() } );
	const auto fragment_shader = _device.createShaderModule(
		{ .codeSize = fragment_code.get_size() * sizeof( uint32_t ), .pCode = fragment_code.get_data() } );

	const vk::PushConstantRange constants { .stageFlags = vk::ShaderStageFlagBits::eAllGraphics, .size = desc.push_constants_size };
	const auto desc_layout = bindless_manager.get_layout();
	auto layout = _device.createPipelineLayout(
		{ .setLayoutCount = 1, .pSetLayouts = &desc_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &constants } );

	const vk::PipelineVertexInputStateCreateInfo vertex_input;
	const vk::PipelineInputAssemblyStateCreateInfo ia { .topology = vk::PrimitiveTopology::eTriangleList };
	const vk::PipelineViewportStateCreateInfo viewport { .viewportCount = 1, .scissorCount = 1 };
	const vk::PipelineRasterizationStateCreateInfo rasterizer { .polygonMode = vk::PolygonMode::eFill,
																.cullMode = vk::CullModeFlagBits::eNone,
																.frontFace = vk::FrontFace::eClockwise,
																.lineWidth = 1.f };
	const vk::PipelineMultisampleStateCreateInfo multisampling { .rasterizationSamples = vk::SampleCountFlagBits::e4,
																 .minSampleShading = 1.0f };
	const vk::PipelineDepthStencilStateCreateInfo depth_stencil = { .depthTestEnable = true,
																	.depthWriteEnable = true,
																	.depthCompareOp = vk::CompareOp::eGreaterOrEqual,
																	.maxDepthBounds = 1.f };
	const vk::PipelineColorBlendAttachmentState blend_attachment { .colorWriteMask = vk::ColorComponentFlagBits::eR
																	   | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
																	   | vk::ColorComponentFlagBits::eA };
	const vk::PipelineColorBlendStateCreateInfo blend_state = { .logicOp = vk::LogicOp::eCopy,
																.attachmentCount = 1,
																.pAttachments = &blend_attachment };
	const auto color_format = static_cast<vk::Format>( desc.color_format );
	const vk::PipelineRenderingCreateInfo render_info { .colorAttachmentCount = 1,
														.pColorAttachmentFormats = &color_format,
														.depthAttachmentFormat = static_cast<vk::Format>( desc.depth_format ) };

	const std::array<vk::DynamicState, 2> state { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	const vk::PipelineDynamicStateCreateInfo dynamic_state { .dynamicStateCount = state.size(), .pDynamicStates = state.data() };

	const std::array<vk::PipelineShaderStageCreateInfo, 2> shaders {
		{ { .stage = vk::ShaderStageFlagBits::eVertex, .module = vertex_shader, .pName = "main" },
		  { .stage = vk::ShaderStageFlagBits::eFragment, .module = fragment_shader, .pName = "main" } }
	};

	const vk::GraphicsPipelineCreateInfo pipeline_info = { .pNext = &render_info,
														   .stageCount = shaders.size(),
														   .pStages = shaders.data(),
														   .pVertexInputState = &vertex_input,
														   .pInputAssemblyState = &ia,
														   .pViewportState = &viewport,
														   .pRasterizationState = &rasterizer,
														   .pMultisampleState = &multisampling,
														   .pDepthStencilState = &depth_stencil,
														   .pColorBlendState = &blend_state,
														   .pDynamicState = &dynamic_state,
														   .layout = layout };

	auto pipeline = _device.createGraphicsPipeline( nullptr, pipeline_info );

	return raii::Pipeline( std::move( layout ), std::move( pipeline ), desc );
}

void renderer::Device::submit( CommandBuffer& buffer, vk::Fence signal_fence )
{
	const vk::CommandBufferSubmitInfo info { .commandBuffer = buffer._cmd_buffer };
	_gfx_queue.submit2( vk::SubmitInfo2 { .commandBufferInfoCount = 1, .pCommandBufferInfos = &info }, signal_fence );
}
