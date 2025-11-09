#pragma once

#include <renderer/common.h>

namespace renderer
{
	class CommandBuffer;

	class Texture
	{
	public:
		enum class Format : std::underlying_type_t<vk::Format>
		{
			UNDEFINED = vk::Format::eUndefined,
			R8G8B8A8_UNORM = vk::Format::eR8G8B8A8Unorm,
			R8G8B8A8_SRGB = vk::Format::eR8G8B8A8Srgb,
			R16G16B16A16_SFLOAT = vk::Format::eR16G16B16A16Sfloat,
			D32_SFLOAT = vk::Format::eD32Sfloat
		};

		enum class Usage : std::underlying_type_t<vk::ImageUsageFlagBits>
		{
			TRANSFER_SRC = vk::ImageUsageFlagBits::eTransferSrc,
			TRANSFER_DST = vk::ImageUsageFlagBits::eTransferDst,
			COLOR_ATTACHMENT = vk::ImageUsageFlagBits::eColorAttachment,
			DEPTH_STENCIL_ATTACHMENT = vk::ImageUsageFlagBits::eDepthStencilAttachment,
		};

		void transition( CommandBuffer& cmd, vk::ImageLayout from, vk::ImageLayout to );

		Format get_format() const { return _format; }
		Usage get_usage() const { return _usage; }
		Extent2D get_extent() const { return _extent; }
		int get_samples() const { return _samples; }

		// private:
		vk::Image _image;
		Format _format;
		Usage _usage;
		Extent2D _extent;
		int _samples = 1;

		friend class Device;
		friend class Swapchain;
	};

	inline constexpr Texture::Usage operator|( Texture::Usage lhs, Texture::Usage rhs )
	{
		return Texture::Usage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	class OwnedTexture : public Texture
	{
	public:
		OwnedTexture() = default;
		~OwnedTexture() { _allocation.destroy( _image ); }
		OwnedTexture( const OwnedTexture& ) = delete;
		OwnedTexture& operator=( const OwnedTexture& ) = delete;

		OwnedTexture( OwnedTexture&& rhs ) noexcept
			: Texture( rhs )
			, _allocation( rhs._allocation )
		{
			rhs = {};
		}

		OwnedTexture& operator=( OwnedTexture&& rhs ) noexcept
		{
			static_cast<Texture&>( *this ) = static_cast<Texture&>( rhs );
			_allocation = rhs._allocation;
			rhs = {};
			return *this;
		}

	private:
		OwnedTexture( const Texture& Desc, const vma::raii::Allocation& allocation )
			: Texture( Desc )
			, _allocation( allocation )
		{
		}

		friend class Device;
		vma::raii::Allocation _allocation;
	};

	class TextureView
	{
	public:
		enum class Aspect : std::underlying_type_t<vk::ImageAspectFlagBits>
		{
			COLOR = vk::ImageAspectFlagBits::eColor,
			DEPTH = vk::ImageAspectFlagBits::eDepth
		};

		// private:
		vk::ImageView _view;

		friend class CommandBuffer;
		friend class Device;
	};

	struct OwnedTextureView
	{
		operator TextureView() const { return { _view }; }

		vk::raii::ImageView _view;
	};
}
