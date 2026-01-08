#include "buffer.h"

#include <cassert>

void renderer::raii::Buffer::map()
{
	assert( _mapped_address == nullptr );
	const auto ret = vmaMapMemory( _allocation.allocator, _allocation.allocation, &_mapped_address );
	if ( ret )
	{
		throw Error( "Failed to map buffer", ret );
	}
}

void renderer::raii::Buffer::unmap()
{
	assert( _mapped_address != nullptr );
	vmaUnmapMemory( _allocation.allocator, _allocation.allocation );
	_mapped_address = nullptr;
}

vk::DeviceAddress renderer::Buffer::get_device_address() const
{
	assert( ( _usage & Usage::SHADER_DEVICE_ADDRESS ) == Usage::SHADER_DEVICE_ADDRESS );
	return _address;
}

void* renderer::Buffer::get_mapped_address() const
{
	assert( _mapped_address != nullptr );
	return _mapped_address;
}
