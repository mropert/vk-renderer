#include "swapchain.h"

#include <VkBootstrap.h>
#include <renderer/device.h>

renderer::Swapchain::Swapchain( Device& device, VkFormat format )
{
	auto swapchain_ret = vkb::SwapchainBuilder( *device._physical_device,
												*device._device,
												*device._surface,
												device._gfx_queue_family_index,
												device._present_queue_family_index )
							 .set_desired_format( VkSurfaceFormatKHR { .format = format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
							 .set_desired_present_mode( VK_PRESENT_MODE_FIFO_KHR )
							 .set_desired_extent( device._extent.width, device._extent.height )
							 .add_image_usage_flags( VK_IMAGE_USAGE_TRANSFER_DST_BIT )
							 .build();

	if ( !swapchain_ret )
	{
		throw renderer_error( swapchain_ret.error(), swapchain_ret.vk_result() );
	}
	_swapchain = { device._device, swapchain_ret.value() };
	_images = _swapchain.getImages();
}
