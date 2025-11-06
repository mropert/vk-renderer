#include "texture.h"

renderer::Texture::Texture( vma::raii::Image image )
	: _image( std::move( image ) )
{
}

renderer::Texture::Texture( VkImage image )
	: _image { image, vma::raii::ResourceDeleter {} }
{
}
