#include "texture.h"

std::size_t renderer::Texture::get_texel_block_size( Format format )
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

bool renderer::Texture::is_depth_format( Format format )
{
	return format == Format::D16_UNORM || format == Format::D32_SFLOAT;
}
