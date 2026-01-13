#pragma once

#include <renderer/buffer.h>
#include <renderer/common.h>
#include <renderer/sampler.h>
#include <renderer/texture.h>
#include <tuple>

namespace renderer
{
	struct BindlessTexture
	{
		struct Handles
		{
			TextureView view;
			uint32_t texture_index = static_cast<uint32_t>( -1 );
			uint32_t storage_index = static_cast<uint32_t>( -1 );
		};

		Texture texture;
		Handles handles;
		std::vector<Handles> mips;
	};

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
		enum class Sets : uint32_t
		{
			TEXTURES = 0,
			BUFFERS = 1,
			COUNT
		};
		static constexpr uint32_t SETS_COUNT = std::to_underlying( Sets::COUNT );

		enum class TextureBindings : uint32_t
		{
			TEXTURES = 0,
			IMAGES = 1,
			LINEAR_SAMPLER = 2,
			LINEAR_MIN_SAMPLER = 3,
			COUNT
		};

		static constexpr uint32_t MAX_TEXTURES = 8192;

		std::array<vk::DescriptorSetLayout, SETS_COUNT> get_layouts() const
		{
			std::array<vk::DescriptorSetLayout, SETS_COUNT> layouts;
			std::copy( begin( _layouts ), end( _layouts ), begin( layouts ) );
			return layouts;
		}
		std::array<vk::DescriptorSet, SETS_COUNT> get_sets() const
		{
			{
				std::array<vk::DescriptorSet, SETS_COUNT> sets;
				std::copy( begin( _sets ), end( _sets ), begin( sets ) );
				return sets;
			}
		}

		BindlessTexture add_texture( raii::Texture&& tex, bool individual_mips = false );
		std::size_t get_texture_memory_usage() const { return _texture_memory; }

	protected:
		BindlessManagerBase( Device& device, std::span<const uint32_t> buffer_capacities );

		uint32_t add_buffer_entry( uint32_t buffer_index, const void* data, uint32_t size );

	private:
		void add_texture_bindings( const Texture::Usage usage, BindlessTexture::Handles& handles );

		Device* _device;
		std::array<vk::raii::DescriptorSetLayout, std::to_underlying( Sets::COUNT )> _layouts = { { nullptr, nullptr } };
		vk::raii::DescriptorPool _pool = nullptr;
		std::array<vk::raii::DescriptorSet, std::to_underlying( Sets::COUNT )> _sets = { { nullptr, nullptr } };
		std::vector<raii::Texture> _textures;
		std::vector<raii::TextureView> _texture_views;
		uint32_t _read_only_textures = 0;
		uint32_t _read_write_textures = 0;
		std::size_t _texture_memory = 0;
		renderer::raii::Sampler _linear_sampler;
		renderer::raii::Sampler _linear_min_sampler;
		std::vector<BindlessBuffer> _buffers;
	};

	// Bindless manager for pipelines
	// Shader layout:
	// - set 0, binding 0: texture (sampled image) array
	// - set 0, binding 1: storage image array
	// - set 0, binding 2: linear sampler
	// - set 0, binding 3: linear min sampler
	// - set 0, bindings 4-N: reserved for future samplers
	// - set 1, binding 0: storage buffer for type #1
	// - set 1, binding 1: storage buffer for type #2
	// ...
	// - set 1, binding N-1: storage buffer for type #N
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
