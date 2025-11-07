#include "command_buffer.h"

void renderer::CommandBuffer::begin()
{
	_cmd_buffer.begin( vk::CommandBufferBeginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
}

void renderer::CommandBuffer::end()
{
	_cmd_buffer.end();
}

void renderer::CommandBuffer::reset()
{
	_cmd_buffer.reset();
}
