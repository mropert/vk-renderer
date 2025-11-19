#pragma once

#include <renderer/common.h>

namespace renderer
{
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

		enum class Layout : std::underlying_type_t<vk::ImageLayout>
		{
			UNDEFINED = vk::ImageLayout::eUndefined,
			GENERAL = vk::ImageLayout::eGeneral,
			COLOR_ATTACHMENT_OPTIMAL = vk::ImageLayout::eColorAttachmentOptimal,
			DEPTH_ATTACHMENT_OPTIMAL = vk::ImageLayout::eDepthAttachmentOptimal,
			DEPTH_READ_ONLY_OPTIMAL = vk::ImageLayout::eDepthReadOnlyOptimal,
			SHADER_READ_ONLY_OPTIMAL = vk::ImageLayout::eShaderReadOnlyOptimal,
			TRANSFER_SRC_OPTIMAL = vk::ImageLayout::eTransferSrcOptimal,
			TRANSFER_DST_OPTIMAL = vk::ImageLayout::eTransferDstOptimal,
			PRESENT_SRC = vk::ImageLayout::ePresentSrcKHR
		};

		enum class Usage : std::underlying_type_t<vk::ImageUsageFlagBits>
		{
			TRANSFER_SRC = vk::ImageUsageFlagBits::eTransferSrc,
			TRANSFER_DST = vk::ImageUsageFlagBits::eTransferDst,
			SAMPLED = vk::ImageUsageFlagBits::eSampled,
			COLOR_ATTACHMENT = vk::ImageUsageFlagBits::eColorAttachment,
			DEPTH_STENCIL_ATTACHMENT = vk::ImageUsageFlagBits::eDepthStencilAttachment,
		};

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
		Layout _layout = Layout::UNDEFINED;

		friend class CommandBuffer;
		friend class Device;
		friend class Swapchain;
	};

	inline constexpr Texture::Usage operator|( Texture::Usage lhs, Texture::Usage rhs )
	{
		return Texture::Usage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

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

	namespace raii
	{
		class Texture : public renderer::Texture
		{
		public:
			Texture() = default;
			~Texture() { _allocation.destroy( _image ); }
			Texture( const Texture& ) = delete;
			Texture& operator=( const Texture& ) = delete;

			Texture( Texture&& rhs ) noexcept
				: renderer::Texture( rhs )
				, _allocation( rhs._allocation )
			{
				rhs.clear();
			}

			Texture& operator=( Texture&& rhs ) noexcept
			{
				static_cast<renderer::Texture&>( *this ) = static_cast<renderer::Texture&>( rhs );
				_allocation = rhs._allocation;
				rhs.clear();
				return *this;
			}

		private:
			Texture( const renderer::Texture& Desc, const vma::raii::Allocation& allocation )
				: renderer::Texture( Desc )
				, _allocation( allocation )
			{
			}

			void clear()
			{
				*static_cast<renderer::Texture*>( this ) = {};
				_allocation = {};
			}

			friend class Device;
			vma::raii::Allocation _allocation;
		};

		class TextureView
		{
		public:
			operator renderer::TextureView() const { return { _view }; }

		private:
			explicit TextureView( vk::raii::ImageView view )
				: _view( std::move( view ) )
			{
			}

			friend class Device;
			vk::raii::ImageView _view;
		};
	}
}
