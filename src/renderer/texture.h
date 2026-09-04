#pragma once

#include <renderer/common.h>

namespace renderer
{
	class BindlessManagerBase;
	class CommandBuffer;
	class Device;
	class Swapchain;

	namespace raii
	{
		class Texture;
		class TextureView;
	}

	class Texture
	{
	public:
		enum class Format : std::underlying_type_t<vk::Format>
		{
			UNDEFINED = std::to_underlying( vk::Format::eUndefined ),
			R8G8B8A8_UNORM = std::to_underlying( vk::Format::eR8G8B8A8Unorm ),
			R8G8B8A8_SRGB = std::to_underlying( vk::Format::eR8G8B8A8Srgb ),
			B8G8R8A8_UNORM = std::to_underlying( vk::Format::eB8G8R8A8Unorm ),
			B8G8R8A8_SRGB = std::to_underlying( vk::Format::eB8G8R8A8Srgb ),
			R16G16B16A16_SFLOAT = std::to_underlying( vk::Format::eR16G16B16A16Sfloat ),
			R32_SFLOAT = std::to_underlying( vk::Format::eR32Sfloat ),
			D32_SFLOAT = std::to_underlying( vk::Format::eD32Sfloat )
		};

		enum class Layout : std::underlying_type_t<vk::ImageLayout>
		{
			UNDEFINED = std::to_underlying( vk::ImageLayout::eUndefined ),
			GENERAL = std::to_underlying( vk::ImageLayout::eGeneral ),
			COLOR_ATTACHMENT_OPTIMAL = std::to_underlying( vk::ImageLayout::eColorAttachmentOptimal ),
			DEPTH_ATTACHMENT_OPTIMAL = std::to_underlying( vk::ImageLayout::eDepthAttachmentOptimal ),
			DEPTH_READ_ONLY_OPTIMAL = std::to_underlying( vk::ImageLayout::eDepthReadOnlyOptimal ),
			SHADER_READ_ONLY_OPTIMAL = std::to_underlying( vk::ImageLayout::eShaderReadOnlyOptimal ),
			TRANSFER_SRC_OPTIMAL = std::to_underlying( vk::ImageLayout::eTransferSrcOptimal ),
			TRANSFER_DST_OPTIMAL = std::to_underlying( vk::ImageLayout::eTransferDstOptimal ),
			PRESENT_SRC = std::to_underlying( vk::ImageLayout::ePresentSrcKHR )
		};

		enum class Usage : std::underlying_type_t<vk::ImageUsageFlagBits>
		{
			NONE = 0,
			TRANSFER_SRC = std::to_underlying( vk::ImageUsageFlagBits::eTransferSrc ),
			TRANSFER_DST = std::to_underlying( vk::ImageUsageFlagBits::eTransferDst ),
			SAMPLED = std::to_underlying( vk::ImageUsageFlagBits::eSampled ),
			STORAGE = std::to_underlying( vk::ImageUsageFlagBits::eStorage ),
			COLOR_ATTACHMENT = std::to_underlying( vk::ImageUsageFlagBits::eColorAttachment ),
			DEPTH_STENCIL_ATTACHMENT = std::to_underlying( vk::ImageUsageFlagBits::eDepthStencilAttachment )
		};

		struct Desc
		{
			Format format = Format::UNDEFINED;
			Usage usage = Usage::NONE;
			Extent2D extent;
			int mips = 1;
			int samples = 1;
		};

		Texture() = default;

		Format get_format() const { return _desc.format; }
		Usage get_usage() const { return _desc.usage; }
		Extent2D get_extent() const { return _desc.extent; }
		int get_mips() const { return _desc.mips; }
		int get_samples() const { return _desc.samples; }

		std::size_t get_size() const { return _desc.extent.width * _desc.extent.height * get_bpp( _desc.format ); }

		static std::size_t get_bpp( Format format );

	protected:
		Texture( vk::Image image, const Desc& desc )
			: _image( image )
			, _desc( desc )
		{
		}

		vk::Image _image;

	private:
		Desc _desc;

		friend class CommandBuffer;
		friend class Device;
		friend class Swapchain;
	};

	inline constexpr Texture::Usage operator|( Texture::Usage lhs, Texture::Usage rhs )
	{
		return Texture::Usage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	inline constexpr Texture::Usage operator&( Texture::Usage lhs, Texture::Usage rhs )
	{
		return Texture::Usage( std::to_underlying( lhs ) & std::to_underlying( rhs ) );
	}

	class TextureView
	{
	public:
		enum class Aspect : std::underlying_type_t<vk::ImageAspectFlagBits>
		{
			COLOR = std::to_underlying( vk::ImageAspectFlagBits::eColor ),
			DEPTH = std::to_underlying( vk::ImageAspectFlagBits::eDepth )
		};

		TextureView() = default;

		Extent2D get_extent() const { return _extent; }

	protected:
		TextureView( vk::ImageView view, Extent2D extent )
			: _view( view )
			, _extent( extent )
		{
		}
		vk::ImageView _view;

	private:
		// Those fields are a repeat of the underlying descriptor, but they make the API more convenient to use
		Extent2D _extent;

		friend class BindlessManagerBase;
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
			Texture( vk::Image image, const Desc& Desc, const vma::raii::Allocation& allocation )
				: renderer::Texture( image, Desc )
				, _allocation( allocation )
			{
			}

			void clear()
			{
				*static_cast<renderer::Texture*>( this ) = { };
				_allocation = { };
			}

			friend class renderer::Device;
			vma::raii::Allocation _allocation;
		};

		class TextureView : public renderer::TextureView
		{
		public:
			TextureView() = default;
			~TextureView()
			{
				if ( _view )
				{
					_device.destroyImageView( _view );
				};
			}
			TextureView( const TextureView& ) = delete;
			TextureView& operator=( const TextureView& ) = delete;

			TextureView( TextureView&& rhs ) noexcept
				: renderer::TextureView( rhs )
				, _device( rhs._device )
				, _dispatcher( rhs._dispatcher )
			{
				rhs.clear();
			}

			TextureView& operator=( TextureView&& rhs ) noexcept
			{
				static_cast<renderer::TextureView&>( *this ) = static_cast<renderer::TextureView&>( rhs );
				_device = rhs._device;
				_dispatcher = rhs._dispatcher;
				rhs.clear();
				return *this;
			}

		private:
			TextureView( vk::raii::ImageView view, Extent2D extent )
				: renderer::TextureView( view, extent )
				, _device( view.getDevice() )
				, _dispatcher( view.getDispatcher() )
			{
				view.release();
			}

			void clear()
			{
				*static_cast<renderer::TextureView*>( this ) = { };
				_device = nullptr;
				_dispatcher = nullptr;
			}

			friend class renderer::Device;
			vk::Device _device = nullptr;
			const vk::raii::detail::DeviceDispatcher* _dispatcher = nullptr;
		};
	}
}
