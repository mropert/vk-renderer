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
{
	_samplers.reserve( 2 );
	_samplers.push_back( device.create_sampler( Sampler::Filter::LINEAR ) );
	_samplers.push_back( device.create_sampler( Sampler::Filter::LINEAR, Sampler::ReductionMode::MIN ) );

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
		  { .binding = std::to_underlying( TextureBindings::SAMPLERS ),
			.descriptorType = vk::DescriptorType::eSampler,
			.descriptorCount = MAX_SAMPLERS,
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
														  { .type = vk::DescriptorType::eSampler, .descriptorCount = MAX_SAMPLERS },
														  { .type = vk::DescriptorType::eStorageBuffer,
															.descriptorCount = static_cast<uint32_t>( _buffers.size() ) } } };
	// We don't need (or want) individual descriptor set deletion but VulkanHpp RAII is all or nothing :(
	const vk::DescriptorPoolCreateInfo pool_info { .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
												   .maxSets = static_cast<uint32_t>( ( MAX_TEXTURES * 2 ) + MAX_SAMPLERS
																					 + _buffers.size() ),
												   .poolSizeCount = pools.size(),
												   .pPoolSizes = pools.data() };
	_pool = device._device.createDescriptorPool( pool_info );

	const vk::DescriptorSetAllocateInfo alloc_info { .descriptorPool = _pool,
													 .descriptorSetCount = SETS_COUNT,
													 .pSetLayouts = get_layouts().data() };
	auto descs = device._device.allocateDescriptorSets( alloc_info );
	assert( descs.size() == SETS_COUNT );
	std::move( begin( descs ), end( descs ), begin( _sets ) );

	std::vector<vk::DescriptorImageInfo> samplers_info;
	samplers_info.reserve( _samplers.size() );
	for ( const renderer::Sampler sampler : _samplers )
	{
		samplers_info.push_back( { .sampler = sampler._sampler } );
	};

	std::vector<vk::DescriptorBufferInfo> buffers_info( _buffers.size() );
	std::vector<vk::WriteDescriptorSet> writes;
	writes.reserve( 1 + _buffers.size() );
	writes.push_back( { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
						.dstBinding = std::to_underlying( TextureBindings::SAMPLERS ),
						.descriptorCount = static_cast<uint32_t>( samplers_info.size() ),
						.descriptorType = vk::DescriptorType::eSampler,
						.pImageInfo = samplers_info.data() } );

	for ( uint32_t i = 0; i < _buffers.size(); ++i )
	{
		// Should we limit the buffer range to the actual used size? Would it be worth the cost of doing a descriptor update on each append?
		buffers_info[ i ] = { .buffer = _buffers[ i ]._buffer._buffer, .range = _buffers[ i ]._buffer.get_size() };
		writes.push_back( { .dstSet = _sets[ std::to_underlying( Sets::BUFFERS ) ],
							.dstBinding = i,
							.descriptorCount = 1,
							.descriptorType = vk::DescriptorType::eStorageBuffer,
							.pBufferInfo = &buffers_info[ i ] } );
	}
	device._device.updateDescriptorSets( writes, {} );
}

renderer::BindlessTexture renderer::BindlessManagerBase::add_texture( raii::Texture&& tex, bool individual_mips )
{
	assert( !individual_mips || tex.get_mips() > 1 );

	const auto aspect = tex.get_format() == Texture::Format::D32_SFLOAT ? TextureView::Aspect::DEPTH : TextureView::Aspect::COLOR;
	auto view = _device->create_texture_view( tex, aspect );

	std::vector<raii::TextureView> mips;
	if ( individual_mips )
	{
		mips.reserve( tex.get_mips() );
		for ( int mip = 0; mip < tex.get_mips(); ++mip )
		{
			mips.push_back( _device->create_texture_view( tex, aspect, mip ) );
		}
	}

	BindlessTexture res { .texture = tex, .handles { .view = view } };
	_texture_memory += tex.get_size();
	_textures.push_back( std::move( tex ) );
	_texture_views.push_back( std::move( view ) );
	add_texture_bindings( _textures.back().get_usage(), res.handles );

	res.mips.reserve( mips.size() );

	for ( auto& mip : mips )
	{
		auto& mip_handle = res.mips.emplace_back( static_cast<TextureView>( mip ) );
		_texture_views.push_back( std::move( mip ) );
		add_texture_bindings( _textures.back().get_usage(), mip_handle );
	}

	return res;
}

void renderer::BindlessManagerBase::add_texture_bindings( const Texture::Usage usage, BindlessTexture::Handles& handles )
{
	std::array<vk::DescriptorImageInfo, 2> infos;
	std::array<vk::WriteDescriptorSet, 2> writes;
	int count = 0;

	if ( ( usage & Texture::Usage::SAMPLED ) == Texture::Usage::SAMPLED )
	{
		handles.texture_index = _read_only_textures++;
		infos[ count ] = { .imageView = handles.view._view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
		writes[ count ] = { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
							.dstBinding = std::to_underlying( TextureBindings::TEXTURES ),
							.dstArrayElement = handles.texture_index,
							.descriptorCount = 1,
							.descriptorType = vk::DescriptorType::eSampledImage,
							.pImageInfo = &infos[ count ] };
		++count;
	}
	if ( ( usage & Texture::Usage::STORAGE ) == Texture::Usage::STORAGE )
	{
		handles.storage_index = _read_write_textures++;
		infos[ count ] = { .imageView = handles.view._view, .imageLayout = vk::ImageLayout::eGeneral };
		writes[ count ] = { .dstSet = _sets[ std::to_underlying( Sets::TEXTURES ) ],
							.dstBinding = std::to_underlying( TextureBindings::IMAGES ),
							.dstArrayElement = handles.storage_index,
							.descriptorCount = 1,
							.descriptorType = vk::DescriptorType::eStorageImage,
							.pImageInfo = &infos[ count ] };
		++count;
	}

	_device->_device.updateDescriptorSets( std::span( writes.data(), count ), {} );
}

uint32_t renderer::BindlessManagerBase::add_buffer_entry( uint32_t buffer_index, const void* data, uint32_t size )
{
	return _buffers[ buffer_index ].append( data, size );
}
