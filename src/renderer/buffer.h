#pragma once

#include <renderer/common.h>

namespace renderer
{
	using BufferHandle = vk::DeviceAddress;

	class Buffer
	{
	public:
		enum class Usage : std::underlying_type_t<vk::BufferUsageFlagBits>
		{
			NONE = 0,
			TRANSFER_SRC = vk::BufferUsageFlagBits::eTransferSrc,
			TRANSFER_DST = vk::BufferUsageFlagBits::eTransferDst,
			UNIFORM_BUFFER = vk::BufferUsageFlagBits::eUniformBuffer,
			STORAGE_BUFFER = vk::BufferUsageFlagBits::eStorageBuffer,
			INDEX_BUFFER = vk::BufferUsageFlagBits::eIndexBuffer,
			INDIRECT_BUFFER = vk::BufferUsageFlagBits::eIndirectBuffer,
			SHADER_DEVICE_ADDRESS = vk::BufferUsageFlagBits::eShaderDeviceAddress,
		};

		Buffer() = default;
		vk::DeviceAddress get_device_address() const;
		void* get_mapped_address() const;
		std::size_t get_size() const { return _size; }
		Usage get_usage() const { return _usage; }

	protected:
		vk::Buffer get_buffer() const { return _buffer; }

	private:
		Buffer( vk::Buffer buffer, vk::DeviceAddress address, void* mapped_address, std::size_t size, Usage usage )
			: _buffer( buffer )
			, _address( address )
			, _mapped_address( mapped_address )
			, _size( size )
			, _usage( usage )
		{
		}

		vk::Buffer _buffer;
		vk::DeviceAddress _address;

	protected:
		void* _mapped_address = nullptr;

	private:
		std::size_t _size = 0;
		Usage _usage = Usage::NONE;

		friend class BindlessManagerBase;
		friend class CommandBuffer;
		friend class Device;
	};

	inline constexpr Buffer::Usage operator|( Buffer::Usage lhs, Buffer::Usage rhs )
	{
		return Buffer::Usage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}
	inline constexpr Buffer::Usage operator&( Buffer::Usage lhs, Buffer::Usage rhs )
	{
		return Buffer::Usage( std::to_underlying( lhs ) & std::to_underlying( rhs ) );
	}

	namespace raii
	{
		class Buffer : public renderer::Buffer
		{
		public:
			Buffer() = default;
			~Buffer() { _allocation.destroy( get_buffer() ); }
			Buffer( const Buffer& ) = delete;
			Buffer& operator=( const Buffer& ) = delete;

			Buffer( Buffer&& rhs ) noexcept
				: renderer::Buffer( rhs )
				, _allocation( rhs._allocation )
			{
				rhs.clear();
			}

			Buffer& operator=( Buffer&& rhs ) noexcept
			{
				static_cast<renderer::Buffer&>( *this ) = static_cast<renderer::Buffer&>( rhs );
				_allocation = rhs._allocation;
				rhs.clear();
				return *this;
			}

			void map();
			void unmap();

		private:
			Buffer( const renderer::Buffer& Desc, const vma::raii::Allocation& allocation )
				: renderer::Buffer( Desc )
				, _allocation( allocation )
			{
			}

			void clear()
			{
				*static_cast<renderer::Buffer*>( this ) = {};
				_allocation = {};
			}

			friend class Device;
			vma::raii::Allocation _allocation;
		};
	}
}
