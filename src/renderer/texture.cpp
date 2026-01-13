#include "texture.h"

std::size_t renderer::Texture::get_bpp( Format format )
{
	switch ( format )
	{
		break;
		case renderer::Texture::Format::R8G8B8A8_UNORM:
		case renderer::Texture::Format::R8G8B8A8_SRGB:
		case renderer::Texture::Format::R32_SFLOAT:
		case renderer::Texture::Format::D32_SFLOAT:
			return 4;
		case renderer::Texture::Format::R16G16B16A16_SFLOAT:
			return 8;
		case renderer::Texture::Format::UNDEFINED:
		default:
			return 0;
	}
}
