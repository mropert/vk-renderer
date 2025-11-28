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
