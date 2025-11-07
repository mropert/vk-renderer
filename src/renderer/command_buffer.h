#pragma once

#include <renderer/common.h>

namespace renderer
{
	class CommandBuffer
	{
	public:
		void begin();
		void end();
		void reset();

	private:
		explicit CommandBuffer( vk::raii::CommandBuffer cmd_buffer )
			: _cmd_buffer( std::move( cmd_buffer ) )
		{
		}

	public:
		vk::raii::CommandBuffer _cmd_buffer;

		friend class Device;
		friend class Swapchain;
		friend class Texture;
	};
}
