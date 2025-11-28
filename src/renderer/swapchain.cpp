#include "swapchain.h"

#include <VkBootstrap.h>
#include <ranges>
#include <renderer/command_buffer.h>
#include <renderer/device.h>
#include <renderer/texture.h>

renderer::Swapchain::Swapchain( Device& device, Texture::Format format )
	: _device( &device )
{
	auto swapchain_ret = vkb::SwapchainBuilder( *device._physical_device,
												*device._device,
												*device._surface,
												device._gfx_queue_family_index,
												device._present_queue_family_index )
							 .set_desired_format( VkSurfaceFormatKHR { .format = static_cast<VkFormat>( format ),
																	   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
							 .set_desired_present_mode( VK_PRESENT_MODE_FIFO_KHR )
							 .set_desired_extent( device._extent.width, device._extent.height )
							 .add_image_usage_flags( VK_IMAGE_USAGE_TRANSFER_DST_BIT )
							 .build();

	if ( !swapchain_ret )
	{
		throw Error( swapchain_ret.error(), swapchain_ret.vk_result() );
	}
	_swapchain = { device._device, swapchain_ret.value() };

	const auto& images = _swapchain.getImages();
	_images.reserve( images.size() );
	_image_views.reserve( _images.size() );
	for ( const auto image : images )
	{
		_images.push_back( Texture { image, format, Texture::Usage::COLOR_ATTACHMENT, device._extent } );
		_image_views.push_back( _device->create_texture_view( _images.back(), TextureView::Aspect::COLOR ) );
	}

	for ( int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i )
	{
		_frames_data[ i ].render_fence = device._device.createFence( vk::FenceCreateInfo { .flags = vk::FenceCreateFlagBits::eSignaled } );
		_frames_data[ i ].render_semaphore = device._device.createSemaphore( vk::SemaphoreCreateInfo() );
		_frames_data[ i ].swapchain_semaphore = device._device.createSemaphore( vk::SemaphoreCreateInfo() );
	}
}

std::tuple<uint32_t, renderer::Texture, renderer::TextureView> renderer::Swapchain::acquire()
{
	const auto frame_index = _frame_count % MAX_FRAMES_IN_FLIGHT;
	if ( const auto result = _device->_device.waitForFences( *_frames_data[ frame_index ].render_fence, vk::True, UINT64_MAX );
		 result != vk::Result::eSuccess )
	{
		throw Error( "Render fence not signaled", result );
	}
	const auto [ result, image_index ] = _swapchain.acquireNextImage( UINT64_MAX, _frames_data[ frame_index ].swapchain_semaphore );
	if ( result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR )
	{
		throw Error( "Failed to acquire swapchain image", result );
	}
	_current_image = image_index;
	_device->_device.resetFences( *_frames_data[ frame_index ].render_fence );
	return { frame_index, _images[ image_index ], _image_views[ image_index ] };
}

void renderer::Swapchain::submit( CommandBuffer& buffer )
{
	const auto frame_index = _frame_count % MAX_FRAMES_IN_FLIGHT;
	const vk::CommandBufferSubmitInfo cmd_submit_info { .commandBuffer = buffer._cmd_buffer };
	const vk::SemaphoreSubmitInfo wait_info { .semaphore = _frames_data[ frame_index ].swapchain_semaphore,
											  .value = 1,
											  .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput };
	const vk::SemaphoreSubmitInfo signal_info { .semaphore = _frames_data[ frame_index ].render_semaphore,
												.value = 1,
												.stageMask = vk::PipelineStageFlagBits2::eAllGraphics };

	_device->_gfx_queue.submit2( vk::SubmitInfo2 { .waitSemaphoreInfoCount = 1,
												   .pWaitSemaphoreInfos = &wait_info,
												   .commandBufferInfoCount = 1,
												   .pCommandBufferInfos = &cmd_submit_info,
												   .signalSemaphoreInfoCount = 1,
												   .pSignalSemaphoreInfos = &signal_info },
								 _frames_data[ frame_index ].render_fence );
}

void renderer::Swapchain::present()
{
	const auto frame_index = _frame_count % MAX_FRAMES_IN_FLIGHT;
	const auto result = _device->_gfx_queue.presentKHR(
		vk::PresentInfoKHR { .waitSemaphoreCount = 1,
							 .pWaitSemaphores = &*_frames_data[ frame_index ].render_semaphore,
							 .swapchainCount = 1,
							 .pSwapchains = &*_swapchain,
							 .pImageIndices = &_current_image } );
	++_frame_count;
	if ( result != vk::Result::eSuccess )
	{
		throw Error( "Failed to present swapchain", result );
	}
}
