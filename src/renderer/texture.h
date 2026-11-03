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
			B5G6R5_UNORM = std::to_underlying( vk::Format::eB5G6R5UnormPack16 ),
			B5G5R5A1_UNORM = std::to_underlying( vk::Format::eB5G5R5A1UnormPack16 ),
			R16G16B16A16_SFLOAT = std::to_underlying( vk::Format::eR16G16B16A16Sfloat ),
			R32_SFLOAT = std::to_underlying( vk::Format::eR32Sfloat ),
			D16_UNORM = std::to_underlying( vk::Format::eD16Unorm ),
			D32_SFLOAT = std::to_underlying( vk::Format::eD32Sfloat ),
			BC7_UNORM = std::to_underlying( vk::Format::eBc7UnormBlock ),
			BC7_SRGB = std::to_underlying( vk::Format::eBc7SrgbBlock )
		};

		// Images live in GENERAL, the others are only for first use and presentation
		enum class Layout : std::underlying_type_t<vk::ImageLayout>
		{
			UNDEFINED = std::to_underlying( vk::ImageLayout::eUndefined ),
			GENERAL = std::to_underlying( vk::ImageLayout::eGeneral ),
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
			int layers = 1;

			size_t get_mip_size( int mip ) const;
			size_t get_size() const;
		};

		Texture() = default;

		Format get_format() const { return _desc.format; }
		Usage get_usage() const { return _desc.usage; }
		Extent2D get_extent() const { return _desc.extent; }
		Extent2D get_extent( int mip ) const
		{
			return { std::max( _desc.extent.width >> mip, 1u ), std::max( _desc.extent.height >> mip, 1u ) };
		}
		int get_mips() const { return _desc.mips; }
		int get_samples() const { return _desc.samples; }
		int get_layers() const { return _desc.layers; }

		size_t get_mip_size( int mip ) const { return _desc.get_mip_size( mip ); }

		// Bytes per texel, or per block of texels for block compressed formats (4x4 texels for BC7)
		static size_t get_texel_block_size( Format format );
		// Texels per block side (4 for block compressed formats, 1 otherwise)
		static size_t get_texel_block_extent( Format format );
		static bool is_depth_format( Format format );

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

	constexpr Texture::Usage operator|( Texture::Usage lhs, Texture::Usage rhs )
	{
		return Texture::Usage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	constexpr Texture::Usage operator&( Texture::Usage lhs, Texture::Usage rhs )
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
				_allocation.destroy( _image );
				static_cast<renderer::Texture&>( *this ) = static_cast<renderer::Texture&>( rhs );
				_allocation = rhs._allocation;
				rhs.clear();
				return *this;
			}

			// Allocated device memory, in bytes (includes all mips and layers, as well as any padding)
			std::size_t get_size() const { return _allocation.info.size; }

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
				if ( _view )
				{
					_device.destroyImageView( _view );
				}
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
