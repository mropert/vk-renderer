#pragma once

#include <renderer/common.h>

namespace renderer
{
	class CommandBuffer
	{
	public:
	private:
		explicit CommandBuffer( vk::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

		vk::CommandBuffer _cmd_buffer;

		friend class Device;
		friend class Swapchain;
	};
}
