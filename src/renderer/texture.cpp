#include "texture.h"

#include <renderer/command_buffer.h>

renderer::Texture::Texture( vma::raii::Image image )
	: _image( std::move( image ) )
{
}

renderer::Texture::Texture( VkImage image )
	: _image { image, vma::raii::ResourceDeleter {} }
{
}

void renderer::Texture::transition( CommandBuffer& cmd, vk::ImageLayout from, vk::ImageLayout to )
{
	const vk::ImageAspectFlags aspectMask = ( to == vk::ImageLayout::eDepthAttachmentOptimal ) ? vk::ImageAspectFlagBits::eDepth
																							   : vk::ImageAspectFlagBits::eColor;
	const vk::ImageMemoryBarrier2 imageBarrier { .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
												 .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
												 .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
												 .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
												 .oldLayout = from,
												 .newLayout = to,
												 .image = _image.get(),
												 .subresourceRange = {
													 .aspectMask = aspectMask,
													 .levelCount = VK_REMAINING_MIP_LEVELS,
													 .layerCount = VK_REMAINING_ARRAY_LAYERS,
												 } };

	cmd._cmd_buffer.pipelineBarrier2( vk::DependencyInfo { .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imageBarrier } );
}
