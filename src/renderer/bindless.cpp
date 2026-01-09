#include "bindless.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <renderer/device.h>

renderer::BindlessManagerBase::BindlessBuffer::BindlessBuffer( Device& device, uint32_t capacity )
{
	// Use uncached memory mapped device memory if possible
	const bool use_mmap = capacity < std::min( 1024zu * 1024, device._properties.transfer_memory_size );
	if ( !use_mmap )
	{
		// TODO: implement support for devices without BAR/ReBAR if needed
		throw Error( "Bindless buffer can't fit into memory mappable device memory" );
	}
	_buffer = device.create_buffer( Buffer::Usage::STORAGE_BUFFER, capacity, true );
}

uint32_t renderer::BindlessManagerBase::BindlessBuffer::append( const void* data, uint32_t size )
{
	if ( _size + size > _buffer.get_size() )
	{
		// TODO: support geometric realloc growth?
		throw Error( "Bindless buffer storage out of space" );
	}
	const auto offset = _size;
	memcpy( static_cast<std::byte*>( _buffer.get_mapped_address() ) + offset, data, size );
	_size += size;
	return offset;
}

renderer::BindlessManagerBase::BindlessManagerBase( Device& device, std::span<const uint32_t> buffer_capacities )
	: _device( &device )
	, _linear_sampler( device.create_sampler( renderer::Sampler::Filter::LINEAR ) )
{
	_buffers.reserve( buffer_capacities.size() );
	for ( const auto capacity : buffer_capacities )
	{
		_buffers.emplace_back( device, capacity );
	}

	// TODO: let user decide which stages the bindless manager can used with?
	const auto stages = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eTaskEXT
		| vk::ShaderStageFlagBits::eVertex;

	std::vector<vk::DescriptorSetLayoutBinding> bindings( 2 + _buffers.size() );
	bindings[ 0 ] = { .binding = 0,
					  .descriptorType = vk::DescriptorType::eSampledImage,
					  .descriptorCount = MAX_TEXTURES,
					  .stageFlags = stages };
	bindings[ 1 ] = { .binding = 1, .descriptorType = vk::DescriptorType::eSampler, .descriptorCount = 1, .stageFlags = stages };

	for ( uint32_t i = 0; i < _buffers.size(); ++i )
	{
		bindings[ i + 2 ] = { .binding = i + 2,
							  .descriptorType = vk::DescriptorType::eStorageBuffer,
							  .descriptorCount = 1,
							  .stageFlags = stages };
	}

	_layout = device._device.createDescriptorSetLayout(
		vk::DescriptorSetLayoutCreateInfo { .bindingCount = static_cast<uint32_t>( bindings.size() ), .pBindings = bindings.data() } );

	const std::array<vk::DescriptorPoolSize, 3> pools { { { .type = vk::DescriptorType::eSampledImage, .descriptorCount = MAX_TEXTURES },
														  { .type = vk::DescriptorType::eSampler, .descriptorCount = 1 },
														  { .type = vk::DescriptorType::eStorageBuffer,
															.descriptorCount = static_cast<uint32_t>( _buffers.size() ) } } };
	// We don't need (or want) individual descriptor set deletion but VulkanHpp RAII is all or nothing :(
	const vk::DescriptorPoolCreateInfo pool_info { .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
												   .maxSets = static_cast<uint32_t>( MAX_TEXTURES + 1 + _buffers.size() ),
												   .poolSizeCount = pools.size(),
												   .pPoolSizes = pools.data() };
	_pool = device._device.createDescriptorPool( pool_info );

	const vk::DescriptorSetAllocateInfo alloc_info { .descriptorPool = _pool, .descriptorSetCount = 1, .pSetLayouts = &*_layout };
	auto descs = device._device.allocateDescriptorSets( alloc_info );
	assert( descs.size() == 1 );
	_set = std::move( descs.front() );

	const vk::DescriptorImageInfo sampler_info { .sampler = static_cast<renderer::Sampler>( _linear_sampler )._sampler };
	std::vector<vk::DescriptorBufferInfo> buffers_info( _buffers.size() );
	std::vector<vk::WriteDescriptorSet> writes( _buffers.size() + 1 );
	writes[ 0 ] = { .dstSet = _set,
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eSampler,
					.pImageInfo = &sampler_info };
	for ( uint32_t i = 0; i < _buffers.size(); ++i )
	{
		// Should we limit the buffer range to the actual used size? Would it be worth the cost of doing a descriptor update on each append?
		buffers_info[ i ] = { .buffer = _buffers[ i ]._buffer._buffer, .range = _buffers[ i ]._buffer.get_size() };
		writes[ i + 1 ] = { .dstSet = _set,
							.dstBinding = i + 2,
							.descriptorCount = 1,
							.descriptorType = vk::DescriptorType::eStorageBuffer,
							.pBufferInfo = &buffers_info[ i ] };
	}
	device._device.updateDescriptorSets( writes, {} );
}

renderer::TextureHandle renderer::BindlessManagerBase::add_texture( raii::Texture&& tex )
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

uint32_t renderer::BindlessManagerBase::add_buffer_entry( uint32_t buffer_index, const void* data, uint32_t size )
{
	return _buffers[ buffer_index ].append( data, size );
}
