#pragma once

#include <renderer/common.h>
#include <renderer/sampler.h>
#include <renderer/texture.h>

namespace renderer
{
	using BufferHandle = vk::DeviceAddress;
	using TextureHandle = uint32_t;

	class Device;

	class BindlessManager
	{
	public:
		static constexpr uint32_t MAX_TEXTURES = 8192;

		explicit BindlessManager( Device& device );

		TextureHandle add_texture( raii::Texture&& tex );
		vk::DescriptorSetLayout get_layout() const { return _layout; }
		vk::DescriptorSet get_set() const { return _set; }

		std::size_t get_texture_memory_usage() const { return _texture_memory; }

	private:
		Device* _device;
		vk::raii::DescriptorSetLayout _layout = nullptr;
		vk::raii::DescriptorPool _pool = nullptr;
		vk::raii::DescriptorSet _set = nullptr;
		std::vector<renderer::raii::Texture> _textures;
		std::vector<renderer::raii::TextureView> _texture_views;
		std::size_t _texture_memory = 0;
		renderer::raii::Sampler _linear_sampler;
	};
}
