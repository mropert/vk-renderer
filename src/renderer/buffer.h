#pragma once

#include <renderer/common.h>

namespace renderer
{
	class Buffer
	{
	public:
		enum class Usage : std::underlying_type_t<vk::BufferUsageFlagBits>
		{
			TRANSFER_SRC = vk::BufferUsageFlagBits::eTransferSrc,
			TRANSFER_DST = vk::BufferUsageFlagBits::eTransferDst,
			STORAGE_BUFFER = vk::BufferUsageFlagBits::eStorageBuffer,
			INDEX_BUFFER = vk::BufferUsageFlagBits::eIndexBuffer,
			SHADER_DEVICE_ADDRESS = vk::BufferUsageFlagBits::eShaderDeviceAddress
		};

		void* get_mapped_address() const { return _mapped_address; }

		// private:
		vk::Buffer _buffer;
		vk::DeviceAddress _address;
		void* _mapped_address;
		std::size_t _size;
		Usage _usage;

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
			~Buffer() { _allocation.destroy( _buffer ); }
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
