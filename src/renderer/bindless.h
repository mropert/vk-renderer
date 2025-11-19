#pragma once

#include <renderer/common.h>

namespace renderer
{
	using BufferHandle = vk::DeviceAddress;
	using TextureHandle = uint32_t;

	class Device;

	class BlindlessManager
	{
	public:
		explicit BlindlessManager( Device& device );

	private:
		Device* _device;
	};
}
