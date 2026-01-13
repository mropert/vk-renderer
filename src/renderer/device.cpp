#include "device.h"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <renderer/bindless.h>
#include <renderer/command_buffer.h>
#include <renderer/details/profiler.h>
#include <renderer/pipeline.h>
#include <renderer/shader.h>

renderer::Device::Device( const char* appname )
{
	OPTICK_EVENT();

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
										.request_validation_layers( true )
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
														.storageBuffer8BitAccess = true,
														.descriptorIndexing = true,
														.samplerFilterMinmax = true,
														.bufferDeviceAddress = true };

	const auto physical_device_ret = vkb::PhysicalDeviceSelector( vk_instance_result.value() )
										 .set_minimum_version( 1, 3 )
										 .set_required_features_13( features13 )
										 .set_required_features_12( features12 )
										 .set_surface( *_surface )
										 .add_desired_extension( VK_EXT_MESH_SHADER_EXTENSION_NAME )
										 .select();

	if ( !physical_device_ret )
	{
		throw Error( physical_device_ret.error(), physical_device_ret.vk_result() );
	}

	_physical_device = { _instance, physical_device_ret.value() };

	set_properties();
	_properties.mesh_shader_support = physical_device_ret->is_extension_present( VK_EXT_MESH_SHADER_EXTENSION_NAME );

	vkb::DeviceBuilder device_builder( physical_device_ret.value() );
	VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_feature { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
																.taskShader = VK_TRUE,
																.meshShader = VK_TRUE };
	if ( _properties.mesh_shader_support )
	{
		device_builder.add_pNext( &mesh_shader_feature );
	}
	const auto device_ret = device_builder.build();
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

#ifdef USE_OPTICK
	VkDevice optick_device = *_device;
	VkPhysicalDevice optick_physical_device = *_physical_device;
	VkQueue optick_queue = *_gfx_queue;
	Optick::InitGpuVulkan( &optick_device, &optick_physical_device, &optick_queue, &_gfx_queue_family_index, 1, nullptr );
#endif
}

renderer::Device::~Device() = default;

void renderer::Device::wait_idle()
{
	_device.waitIdle();
}

renderer::raii::CommandBuffer renderer::Device::grab_command_buffer()
{
	auto cmd = _available_command_buffers.front();
	_available_command_buffers.pop();
	return raii::CommandBuffer( cmd, raii::CommandBufferDeleter { this } );
}

void renderer::Device::release_command_buffer( CommandBuffer* buffer )
{
	_available_command_buffers.push( buffer );
}

renderer::raii::Texture renderer::Device::create_texture( const Texture::Desc& desc )
{
	OPTICK_EVENT();
	const VkImageCreateInfo info { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								   .imageType = VK_IMAGE_TYPE_2D,
								   .format = static_cast<VkFormat>( desc.format ),
								   .extent = { .width = desc.extent.width, .height = desc.extent.height, .depth = 1 },
								   .mipLevels = static_cast<uint32_t>( desc.mips ),
								   .arrayLayers = 1,
								   .samples = static_cast<VkSampleCountFlagBits>( desc.samples ),
								   .tiling = VK_IMAGE_TILING_OPTIMAL,
								   .usage = static_cast<VkImageUsageFlags>( desc.usage ) };

	const VmaAllocationCreateInfo create_info { .usage = VMA_MEMORY_USAGE_AUTO };

	VkImage image {};
	VmaAllocation allocation {};
	VmaAllocationInfo allocation_info {};
	auto ret = vmaCreateImage( _allocator.get(), &info, &create_info, &image, &allocation, &allocation_info );
	if ( ret )
	{
		throw Error( "Failed to create image", ret );
	}

	return raii::Texture( image, desc, vma::raii::Allocation { _allocator.get(), allocation, allocation_info } );
}

renderer::raii::TextureView renderer::Device::create_texture_view( const Texture& texture, TextureView::Aspect aspect, int mip_level )
{
	const vk::ImageViewCreateInfo image_view_info { .image = texture._image,
													.viewType = vk::ImageViewType::e2D,
													.format = static_cast<vk::Format>( texture.get_format() ),
													.subresourceRange = {
														.aspectMask = static_cast<vk::ImageAspectFlagBits>( aspect ),
														.baseMipLevel = static_cast<uint32_t>( mip_level == -1 ? 0 : mip_level ),
														.levelCount = mip_level == -1 ? VK_REMAINING_MIP_LEVELS : 1,
														.baseArrayLayer = 0,
														.layerCount = 1 } };


	auto view = _device.createImageView( image_view_info );
	return raii::TextureView( std::move( view ) );
}

renderer::raii::Sampler renderer::Device::create_sampler( Sampler::Filter filter, Sampler::ReductionMode mode )
{
	OPTICK_EVENT();
	const vk::SamplerReductionModeCreateInfo reduction_info { .reductionMode = static_cast<vk::SamplerReductionMode>( mode ) };
	auto sampler = _device.createSampler( vk::SamplerCreateInfo { .pNext = &reduction_info,
																  .magFilter = static_cast<vk::Filter>( filter ),
																  .minFilter = static_cast<vk::Filter>( filter ) } );
	return raii::Sampler( std::move( sampler ) );
}

renderer::raii::Buffer renderer::Device::create_buffer( Buffer::Usage usage, std::size_t size, bool upload )
{
	OPTICK_EVENT();
	const VkBufferCreateInfo buffer_Info { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
										   .size = size,
										   .usage = static_cast<VkBufferUsageFlags>( usage ) };
	const VmaAllocationCreateInfo vma_alloc_info = {
		.flags = upload ? ( VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT ) : 0u,
		.usage = VMA_MEMORY_USAGE_AUTO
	};

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

	return raii::Buffer( renderer::Buffer( buffer, address, allocation_info.pMappedData, size, usage ),
						 vma::raii::Allocation { _allocator.get(), allocation, allocation_info } );
}

vk::raii::PipelineLayout renderer::Device::create_pipeline_layout( vk::ShaderStageFlags used_stages,
																   uint32_t push_constants_size,
																   const BindlessManagerBase& bindless_manager )
{
	const vk::PushConstantRange constants { .stageFlags = used_stages, .size = push_constants_size };
	const auto desc_layouts = bindless_manager.get_layouts();
	vk::PipelineLayoutCreateInfo layout_create_info { .setLayoutCount = desc_layouts.size(), .pSetLayouts = desc_layouts.data() };
	if ( constants.size > 0 )
	{
		layout_create_info.pushConstantRangeCount = 1;
		layout_create_info.pPushConstantRanges = &constants;
	}
	return _device.createPipelineLayout( layout_create_info );
}

renderer::raii::Pipeline renderer::Device::create_graphics_pipeline( const Pipeline::Desc& desc,
																	 std::span<const ShaderCode> shaders,
																	 const BindlessManagerBase& bindless_manager )
{
	OPTICK_EVENT();

	vk::ShaderStageFlags used_stages {};
	for ( const auto& shader : shaders )
	{
		used_stages |= static_cast<vk::ShaderStageFlagBits>( shader._stage );
	}

	auto layout = create_pipeline_layout( used_stages, desc.push_constants_size, bindless_manager );

	const vk::PipelineVertexInputStateCreateInfo vertex_input;
	const vk::PipelineInputAssemblyStateCreateInfo ia { .topology = static_cast<vk::PrimitiveTopology>( desc.topology ) };
	const vk::PipelineViewportStateCreateInfo viewport { .viewportCount = 1, .scissorCount = 1 };
	const vk::PipelineRasterizationStateCreateInfo rasterizer { .polygonMode = vk::PolygonMode::eFill,
																.cullMode = static_cast<vk::CullModeFlagBits>( desc.cull_mode ),
																.frontFace = static_cast<vk::FrontFace>( desc.front_face ),
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

	std::vector<vk::raii::ShaderModule> modules;
	std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;

	for ( const auto& shader : shaders )
	{
		modules.push_back( _device.createShaderModule( { .codeSize = shader._blob.size_bytes(), .pCode = shader._blob.data() } ) );
		shader_stages.push_back(
			{ .stage = static_cast<vk::ShaderStageFlagBits>( shader._stage ), .module = modules.back(), .pName = "main" } );
	}

	const vk::GraphicsPipelineCreateInfo pipeline_info = { .pNext = &render_info,
														   .stageCount = static_cast<uint32_t>( shader_stages.size() ),
														   .pStages = shader_stages.data(),
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

	return raii::Pipeline( std::move( layout ), std::move( pipeline ), desc, used_stages, Pipeline::Type::Graphics );
}

renderer::raii::Pipeline
renderer::Device::create_compute_pipeline( const Pipeline::Desc& desc, ShaderCode shader, const BindlessManagerBase& bindless_manager )
{
	const vk::ShaderStageFlags used_stages = vk::ShaderStageFlagBits::eCompute;
	auto layout = create_pipeline_layout( used_stages, desc.push_constants_size, bindless_manager );

	const auto shader_module = _device.createShaderModule( { .codeSize = shader._blob.size_bytes(), .pCode = shader._blob.data() } );

	const vk::ComputePipelineCreateInfo info {
		.stage { .stage = vk::ShaderStageFlagBits::eCompute, .module = shader_module, .pName = "main" },
		.layout = layout
	};

	auto pipeline = _device.createComputePipeline( nullptr, info );

	return raii::Pipeline( std::move( layout ), std::move( pipeline ), desc, used_stages, Pipeline::Type::Compute );
}

renderer::raii::Fence renderer::Device::create_fence( bool signaled )
{
	OPTICK_EVENT();
	return _device.createFence(
		vk::FenceCreateInfo { .flags = signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlagBits {} } );
}

void renderer::Device::wait_for_fences( std::span<const Fence> fences, uint64_t timeout )
{
	OPTICK_EVENT();
	// We can safely ignore the return value, VulkanHpp already throws an exception on failure
	(void)_device.waitForFences( fences, true, timeout );
}

void renderer::Device::submit( CommandBuffer& buffer, vk::Fence signal_fence )
{
	OPTICK_EVENT();
	const vk::CommandBufferSubmitInfo info { .commandBuffer = buffer._cmd_buffer };
	_gfx_queue.submit2( vk::SubmitInfo2 { .commandBufferInfoCount = 1, .pCommandBufferInfos = &info }, signal_fence );
}

renderer::raii::QueryPool renderer::Device::create_query_pool( uint32_t size )
{
	return _device.createQueryPool( vk::QueryPoolCreateInfo { .queryType = vk::QueryType::eTimestamp, .queryCount = size } );
}

void renderer::Device::get_query_pool_results( QueryPool pool, uint32_t first_index, std::span<uint64_t> results )
{
	// Can't seem to find the C++ wrapper, let's use the C API
	vkGetQueryPoolResults( *_device,
						   pool,
						   first_index,
						   static_cast<uint32_t>( results.size() ),
						   results.size_bytes(),
						   results.data(),
						   sizeof( uint64_t ),
						   VK_QUERY_RESULT_64_BIT );
}

float renderer::Device::get_timestamp_period() const
{
	// XXX: we don't cache device props
	return _physical_device.getProperties().limits.timestampPeriod;
}

void renderer::Device::set_relative_mouse_mode( bool enabled )
{
	SDL_SetWindowRelativeMouseMode( _window.get(), enabled );
}

void renderer::Device::queue_deletion( raii::Pipeline pipeline )
{
	_delete_queue[ _delete_index ].push_back( std::move( pipeline ) );
}

renderer::Device::Internals renderer::Device::get_internals() const
{
	return Internals { .api_version = VK_API_VERSION_1_3,
					   .window = _window.get(),
					   .instance = *_instance,
					   .physical_device = *_physical_device,
					   .device = *_device,
					   .queue_family = _gfx_queue_family_index,
					   .queue = *_gfx_queue };
}

void renderer::Device::notify_present()
{
	OPTICK_EVENT();
	_delete_index = ( _delete_index + 1 ) % MAX_FRAMES_IN_FLIGHT;
	_delete_queue[ _delete_index ].clear();
}

void renderer::Device::set_properties()
{
	const auto props_chain = _physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceMeshShaderPropertiesEXT>();
	_properties.name = props_chain.get<vk::PhysicalDeviceProperties2>().properties.deviceName.data();

	const auto& mesh_shader_props = props_chain.get<vk::PhysicalDeviceMeshShaderPropertiesEXT>();
	_properties.max_mesh_shader_groups = mesh_shader_props.maxMeshWorkGroupTotalCount;
	_properties.max_mesh_shader_group_size = mesh_shader_props.maxMeshWorkGroupCount;

	static constexpr auto bar_flags = vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible;
	static constexpr auto gpu_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
	const auto mem_props = _physical_device.getMemoryProperties();
	// We assume there's (at most) one heap per type (CPU/GPU/BAR)
	for ( const auto type : std::span( mem_props.memoryTypes.data(), mem_props.memoryTypeCount ) )
	{
		if ( ( type.propertyFlags & bar_flags ) == bar_flags )
		{
			_properties.transfer_memory_size = mem_props.memoryHeaps[ type.heapIndex ].size;
		}
		else if ( ( type.propertyFlags & bar_flags ) == gpu_flags )
		{
			_properties.device_memory_size = mem_props.memoryHeaps[ type.heapIndex ].size;
		}
		else
		{
			_properties.host_memory_size = mem_props.memoryHeaps[ type.heapIndex ].size;
		}
	}
}
