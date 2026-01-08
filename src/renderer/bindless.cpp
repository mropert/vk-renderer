#include "bindless.h"

#include <array>
#include <cassert>
#include <renderer/device.h>

renderer::BindlessManager::BindlessManager( Device& device )
	: _device( &device )
	, _linear_sampler( device.create_sampler( renderer::Sampler::Filter::LINEAR ) )
{
	const std::array<vk::DescriptorSetLayoutBinding, 2> bindings { { { .binding = 0,
																	   .descriptorType = vk::DescriptorType::eSampledImage,
																	   .descriptorCount = MAX_TEXTURES,
																	   .stageFlags = vk::ShaderStageFlagBits::eFragment },
																	 { .binding = 1,
																	   .descriptorType = vk::DescriptorType::eSampler,
																	   .descriptorCount = 1,
																	   .stageFlags = vk::ShaderStageFlagBits::eFragment } } };

	_layout = device._device.createDescriptorSetLayout(
		vk::DescriptorSetLayoutCreateInfo { .bindingCount = bindings.size(), .pBindings = bindings.data() } );

	const std::array<vk::DescriptorPoolSize, 2> pools { { { .type = vk::DescriptorType::eSampledImage, .descriptorCount = MAX_TEXTURES },
														  { .type = vk::DescriptorType::eSampler, .descriptorCount = 1 } } };
	// We don't need (or want) individual descriptor set deletion but VulkanHpp RAII is all or nothing :(
	const vk::DescriptorPoolCreateInfo pool_info { .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
												   .maxSets = MAX_TEXTURES + 1,
												   .poolSizeCount = pools.size(),
												   .pPoolSizes = pools.data() };
	_pool = device._device.createDescriptorPool( pool_info );

	const vk::DescriptorSetAllocateInfo alloc_info { .descriptorPool = _pool, .descriptorSetCount = 1, .pSetLayouts = &*_layout };
	auto descs = device._device.allocateDescriptorSets( alloc_info );
	assert( descs.size() == 1 );
	_set = std::move( descs.front() );

	const vk::DescriptorImageInfo sampler_info { .sampler = static_cast<renderer::Sampler>( _linear_sampler )._sampler };
	const vk::WriteDescriptorSet write_set { .dstSet = _set,
											 .dstBinding = 1,
											 .descriptorCount = 1,
											 .descriptorType = vk::DescriptorType::eSampler,
											 .pImageInfo = &sampler_info };
	device._device.updateDescriptorSets( write_set, {} );
}

renderer::TextureHandle renderer::BindlessManager::add_texture( raii::Texture&& tex )
{
	const auto index = static_cast<TextureHandle>( _textures.size() );
	_textures.push_back( std::move( tex ) );
	_texture_memory += _textures.back().get_size();
	_texture_views.push_back( _device->create_texture_view( _textures[ index ], TextureView::Aspect::COLOR ) );

	const vk::DescriptorImageInfo info { .imageView = static_cast<TextureView>( _texture_views[ index ] )._view,
										 .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

	const vk::WriteDescriptorSet write_set { .dstSet = _set,
											 .dstBinding = 0,
											 .dstArrayElement = index,
											 .descriptorCount = 1,
											 .descriptorType = vk::DescriptorType::eSampledImage,
											 .pImageInfo = &info };

	_device->_device.updateDescriptorSets( write_set, {} );

	return index;
}
