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
		| vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute;

	const std::array<vk::DescriptorSetLayoutBinding, std::to_underlying( TextureBindings::COUNT )> texture_bindings {
		{ { .binding = std::to_underlying( TextureBindings::TEXTURES ),
			.descriptorType = vk::DescriptorType::eSampledImage,
			.descriptorCount = MAX_TEXTURES,
			.stageFlags = stages },
		  { .binding = std::to_underlying( TextureBindings::IMAGES ),
			.descriptorType = vk::DescriptorType::eStorageImage,
			.descriptorCount = MAX_TEXTURES,
			.stageFlags = stages },
		  { .binding = std::to_underlying( TextureBindings::LINEAR_SAMPLER ),
			.descriptorType = vk::DescriptorType::eSampler,
			.descriptorCount = 1,
			.stageFlags = stages } }
	};

	_layouts[ std::to_underlying( Sets::TEXTURES ) ] = device._device.createDescriptorSetLayout(
		vk::DescriptorSetLayoutCreateInfo { .bindingCount = static_cast<uint32_t>( texture_bindings.size() ),
											.pBindings = texture_bindings.data() } );

	std::vector<vk::DescriptorSetLayoutBinding> buffer_bindings;
	for ( uint32_t i = 0; i < _buffers.size(); ++i )
	{
		buffer_bindings.push_back(
			{ .binding = i, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = stages } );
	}

	_layouts[ std::to_underlying( Sets::BUFFERS ) ] = device._device.createDescriptorSetLayout(
		vk::DescriptorSetLayoutCreateInfo { .bindingCount = static_cast<uint32_t>( buffer_bindings.size() ),
											.pBindings = buffer_bindings.data() } );

	const std::array<vk::DescriptorPoolSize, 4> pools { { { .type = vk::DescriptorType::eSampledImage, .descriptorCount = MAX_TEXTURES },
														  { .type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_TEXTURES },
														  { .type = vk::DescriptorType::eSampler, .descriptorCount = 1 },
														  { .type = vk::DescriptorType::eStorageBuffer,
															.descriptorCount = static_cast<uint32_t>( _buffers.size() ) } } };
	// We don't need (or want) individual descriptor set deletion but VulkanHpp RAII is all or nothing :(
	const vk::DescriptorPoolCreateInfo pool_info { .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
												   .maxSets = static_cast<uint32_t>( ( MAX_TEXTURES * 2 ) + 1 + _buffers.size() ),
												   .poolSizeCount = pools.size(),
												   .pPoolSizes = pools.data() };
	_pool = device._device.createDescriptorPool( pool_info );

	const vk::DescriptorSetAllocateInfo alloc_info { .descriptorPool = _pool,
													 .descriptorSetCount = SETS_COUNT,
													 .pSetLayouts = get_layouts().data() };
	auto descs = device._device.allocateDescriptorSets( alloc_info );
	assert( descs.size() == SETS_COUNT );
	std::move( begin( descs ), end( descs ), begin( _sets ) );

	const vk::DescriptorImageInfo sampler_info { .sampler = static_cast<renderer::Sampler>( _linear_sampler )._sampler };
	std::vector<vk::DescriptorBufferInfo> buffers_info( _buffers.size() );
	std::vector<vk::WriteDescriptorSet> writes( _buffers.size() + 1 );
	writes[ 0 ] = { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
					.dstBinding = std::to_underlying( TextureBindings::LINEAR_SAMPLER ),
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eSampler,
					.pImageInfo = &sampler_info };
	for ( uint32_t i = 0; i < _buffers.size(); ++i )
	{
		// Should we limit the buffer range to the actual used size? Would it be worth the cost of doing a descriptor update on each append?
		buffers_info[ i ] = { .buffer = _buffers[ i ]._buffer._buffer, .range = _buffers[ i ]._buffer.get_size() };
		writes[ i + 1 ] = { .dstSet = _sets[ std::to_underlying( Sets::BUFFERS ) ],
							.dstBinding = i,
							.descriptorCount = 1,
							.descriptorType = vk::DescriptorType::eStorageBuffer,
							.pBufferInfo = &buffers_info[ i ] };
	}
	device._device.updateDescriptorSets( writes, {} );
}

renderer::BindlessTexture renderer::BindlessManagerBase::add_texture_read_only( raii::Texture&& tex )
{
	auto view = _device->create_texture_view( tex,
											  tex.get_format() == Texture::Format::D32_SFLOAT ? TextureView::Aspect::DEPTH
																							  : TextureView::Aspect::COLOR );

	const auto index = _textures.size();
	const auto read_only_index = _read_only_textures++;
	_textures.push_back( std::move( tex ) );
	_texture_memory += _textures[ index ].get_size();
	_texture_views.push_back( std::move( view ) );

	add_texture_bindings( index, read_only_index );

	return { _textures[ index ], _texture_views[ index ], read_only_index, static_cast<uint32_t>( -1 ) };
}

renderer::BindlessTexture renderer::BindlessManagerBase::add_texture_read_write( raii::Texture&& tex )
{
	assert( ( tex.get_usage() & Texture::Usage::STORAGE ) == Texture::Usage::STORAGE );

	auto view = _device->create_texture_view( tex, TextureView::Aspect::COLOR );

	const auto index = _textures.size();
	const auto read_only_index = _read_only_textures++;
	const auto read_write_index = _read_write_textures++;
	_textures.push_back( std::move( tex ) );
	_texture_memory += _textures[ index ].get_size();
	_texture_views.push_back( std::move( view ) );

	add_texture_bindings( index, read_only_index, read_write_index );

	return { _textures[ index ], _texture_views[ index ], read_only_index, read_write_index };
}

void renderer::BindlessManagerBase::add_texture_bindings( uint32_t view_index, uint32_t read_only_index, uint32_t read_write_index )
{
	const std::array<vk::DescriptorImageInfo, 2> info { { { .imageView = static_cast<TextureView>( _texture_views[ view_index ] )._view,
															.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
														  { .imageView = static_cast<TextureView>( _texture_views[ view_index ] )._view,
															.imageLayout = vk::ImageLayout::eGeneral } } };

	const std::array<vk::WriteDescriptorSet, 2> write_set { { { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
																.dstBinding = std::to_underlying( TextureBindings::TEXTURES ),
																.dstArrayElement = read_only_index,
																.descriptorCount = 1,
																.descriptorType = vk::DescriptorType::eSampledImage,
																.pImageInfo = &info[ 0 ] },
															  { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
																.dstBinding = std::to_underlying( TextureBindings::IMAGES ),
																.dstArrayElement = read_write_index,
																.descriptorCount = 1,
																.descriptorType = vk::DescriptorType::eStorageImage,
																.pImageInfo = &info[ 1 ] } } };

	_device->_device.updateDescriptorSets( std::span( write_set.data(), read_write_index != -1 ? 2 : 1 ), {} );
}

uint32_t renderer::BindlessManagerBase::add_buffer_entry( uint32_t buffer_index, const void* data, uint32_t size )
{
	return _buffers[ buffer_index ].append( data, size );
}
