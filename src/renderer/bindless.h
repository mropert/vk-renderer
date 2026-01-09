#pragma once

#include <renderer/buffer.h>
#include <renderer/common.h>
#include <renderer/sampler.h>
#include <renderer/texture.h>
#include <tuple>

namespace renderer
{
	using TextureHandle = uint32_t;
	template <typename T>
	struct BindlessHandle
	{
		uint32_t index;
	};

	class Device;

	// Internal base class with no type safety for buffer, prefer BindlessManager
	class BindlessManagerBase
	{
		// A basic monotonic buffer with max capacity
		// Could be enhanced with vector-style geometric growth and/or free list delete support if needed
		// (assuming all contained elements have the same size)
		struct BindlessBuffer
		{
			BindlessBuffer( Device& device, uint32_t capacity );
			uint32_t append( const void* data, uint32_t size );

			raii::Buffer _buffer;
			uint32_t _size = 0;
		};

	public:
		static constexpr uint32_t MAX_TEXTURES = 8192;

		vk::DescriptorSetLayout get_layout() const { return _layout; }
		vk::DescriptorSet get_set() const { return _set; }

		TextureHandle add_texture( raii::Texture&& tex );
		std::size_t get_texture_memory_usage() const { return _texture_memory; }

	protected:
		BindlessManagerBase( Device& device, std::span<const uint32_t> buffer_capacities );

		uint32_t add_buffer_entry( uint32_t buffer_index, const void* data, uint32_t size );

	private:
		Device* _device;
		vk::raii::DescriptorSetLayout _layout = nullptr;
		vk::raii::DescriptorPool _pool = nullptr;
		vk::raii::DescriptorSet _set = nullptr;
		std::vector<raii::Texture> _textures;
		std::vector<raii::TextureView> _texture_views;
		std::size_t _texture_memory = 0;
		renderer::raii::Sampler _linear_sampler;
		std::vector<BindlessBuffer> _buffers;
	};

	// Bindless manager for pipelines
	// Shader layout:
	// - set 0, binding 0: texture array
	// - set 0, binding 1: linear sampler
	// - set 0, binding 2: storage buffer for type #1
	// - set 0, binding 3: storage buffer for type #2
	// ...
	// - set 0, binding N-1: storage buffer for type #N
	template <typename... BufferTypes>
	class BindlessManager : public BindlessManagerBase
	{
		using buffers_tuple = std::tuple<BufferTypes...>;
		static constexpr auto buffer_count = std::tuple_size_v<buffers_tuple>;
		static constexpr std::array<uint32_t, buffer_count> buffer_item_size = { { sizeof( BufferTypes )... } };

		template <class T, class Tuple>
		struct buffer_index;

		template <class T, class... Types>
		struct buffer_index<T, std::tuple<T, Types...>>
		{
			static constexpr std::size_t value = 0;
		};

		template <class T, class U, class... Types>
		struct buffer_index<T, std::tuple<U, Types...>>
		{
			static constexpr std::size_t value = 1 + buffer_index<T, std::tuple<Types...>>::value;
		};

		static constexpr std::array<uint32_t, buffer_count> calc_capacities( std::span<const uint32_t, buffer_count> caps )
		{
			auto capacities = buffer_item_size;
			for ( int i = 0; i < buffer_count; ++i )
			{
				capacities[ i ] *= caps[ i ];
			}
			return capacities;
		}

	public:
		BindlessManager( Device& device, std::span<const uint32_t, buffer_count> capacities )
			: BindlessManagerBase( device, calc_capacities( capacities ) )
		{
		}

		template <typename T>
		BindlessHandle<T> add_buffer_entry( const T& value )
		{
			const auto offset = BindlessManagerBase::add_buffer_entry( buffer_index<T, buffers_tuple>::value, &value, sizeof( T ) );
			return { offset / sizeof( T ) };
		}
	};
}
