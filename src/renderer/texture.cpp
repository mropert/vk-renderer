#include "texture.h"

namespace renderer::details
{
	constexpr size_t get_texel_block_size( Texture::Format format )
	{
		switch ( format )
		{
			case renderer::Texture::Format::R8G8B8A8_UNORM:
			case renderer::Texture::Format::R8G8B8A8_SRGB:
			case renderer::Texture::Format::B8G8R8A8_UNORM:
			case renderer::Texture::Format::B8G8R8A8_SRGB:
			case renderer::Texture::Format::R32_SFLOAT:
			case renderer::Texture::Format::D32_SFLOAT:
				return 4;
			case renderer::Texture::Format::R16G16B16A16_SFLOAT:
				return 8;
			case renderer::Texture::Format::B5G6R5_UNORM:
			case renderer::Texture::Format::B5G5R5A1_UNORM:
			case renderer::Texture::Format::D16_UNORM:
				return 2;
			case renderer::Texture::Format::BC7_UNORM:
			case renderer::Texture::Format::BC7_SRGB:
				return 16;
			case renderer::Texture::Format::UNDEFINED:
			default:
				return 0;
		}
	}

	constexpr size_t get_texel_block_extent( Texture::Format format )
	{
		switch ( format )
		{
			case renderer::Texture::Format::BC7_UNORM:
			case renderer::Texture::Format::BC7_SRGB:
				return 4;
		}
		return 1;
	}

	constexpr size_t get_mip_size( const Texture::Desc& desc, int mip )
	{
		const auto block = get_texel_block_extent( desc.format );
		const auto width = std::max( desc.extent.width >> mip, 1u );
		const auto height = std::max( desc.extent.height >> mip, 1u );
		return ( ( width + block - 1 ) / block ) * ( ( height + block - 1 ) / block ) * get_texel_block_size( desc.format );
	}
	constexpr size_t get_size( const Texture::Desc& desc )
	{
		size_t size = 0;
		for ( int mip = 0; mip < desc.mips; ++mip )
			size += get_mip_size( desc, mip );
		return size * desc.layers;
	}

	static_assert( get_mip_size( { .format = Texture::Format::BC7_UNORM, .extent = { 1024, 1024 } }, 10 ) == 16 );
	static_assert( get_mip_size( { .format = Texture::Format::BC7_UNORM, .extent = { 6, 6 } }, 0 ) == 64 );
	static_assert( get_mip_size( { .format = Texture::Format::BC7_UNORM, .extent = { 12, 12 } }, 1 ) == 64 );
	static_assert( get_mip_size( { .format = Texture::Format::BC7_UNORM, .extent = { 1024, 512 } }, 10 ) == 16 );
	static_assert( get_mip_size( { .format = Texture::Format::R8G8B8A8_UNORM, .extent = { 6, 6 } }, 0 ) == 144 );
}

size_t renderer::Texture::Desc::get_mip_size( int mip ) const
{
	return details::get_mip_size( *this, mip );
}

size_t renderer::Texture::Desc::get_size() const
{
	return details::get_size( *this );
}

size_t renderer::Texture::get_texel_block_size( Format format )
{
	return details::get_texel_block_size( format );
}

size_t renderer::Texture::get_texel_block_extent( Format format )
{
	return details::get_texel_block_extent( format );
}

bool renderer::Texture::is_depth_format( Format format )
{
	return format == Format::D16_UNORM || format == Format::D32_SFLOAT;
}
