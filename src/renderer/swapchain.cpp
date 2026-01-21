#include "swapchain.h"

#include <VkBootstrap.h>
#include <ranges>
#include <renderer/command_buffer.h>
#include <renderer/details/profiler.h>
#include <renderer/device.h>
#include <renderer/texture.h>

renderer::Swapchain::Swapchain( Device& device, Texture::Format format, bool vsync )
	: _device( &device )
{
	OPTICK_EVENT();

	_swapchain = create( device, format, vsync, nullptr );

	fill_images( format );

	for ( int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i )
	{
		_frame_fences.push_back( device.create_fence( true ) );
		_acquire_semaphores.push_back( device._device.createSemaphore( vk::SemaphoreCreateInfo() ) );
	}
	for ( int i = 0; i < _images.size(); ++i )
	{
		_submit_semaphores.push_back( device._device.createSemaphore( vk::SemaphoreCreateInfo() ) );
	}
}

void renderer::Swapchain::recreate( Texture::Format format, bool vsync )
{
	auto new_swapchain = create( *_device, format, vsync, *_swapchain );
	_device->wait_idle();
	_image_views.clear();
	_images.clear();
	_swapchain = std::move( new_swapchain );
	fill_images( format );
	for ( int i = _submit_semaphores.size(); i < _images.size(); ++i )
	{
		_submit_semaphores.push_back( _device->_device.createSemaphore( vk::SemaphoreCreateInfo() ) );
	}
}

vk::raii::SwapchainKHR renderer::Swapchain::create( Device& device, Texture::Format format, bool vsync, VkSwapchainKHR old_swapchain )
{
	vkb::SwapchainBuilder builder( *device._physical_device,
								   *device._device,
								   *device._surface,
								   device._gfx_queue_family_index,
								   device._present_queue_family_index );
	builder.set_desired_format(
		VkSurfaceFormatKHR { .format = static_cast<VkFormat>( format ), .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } );
	if ( vsync )
	{
		builder.set_desired_present_mode( VK_PRESENT_MODE_FIFO_KHR );
	}
	else
	{
		builder.set_desired_present_mode( VK_PRESENT_MODE_MAILBOX_KHR );
		builder.add_fallback_present_mode( VK_PRESENT_MODE_IMMEDIATE_KHR );
	}
	builder.set_desired_extent( device._extent.width, device._extent.height );
	builder.add_image_usage_flags( VK_IMAGE_USAGE_TRANSFER_DST_BIT );
	builder.set_old_swapchain( old_swapchain );

	auto swapchain_ret = builder.build();
	if ( !swapchain_ret )
	{
		throw Error( swapchain_ret.error(), swapchain_ret.vk_result() );
	}
	return { device._device, swapchain_ret.value() };
}

void renderer::Swapchain::fill_images( Texture::Format format )
{
	const auto& images = _swapchain.getImages();
	_images.reserve( images.size() );
	_image_views.reserve( _images.size() );
	for ( const auto image : images )
	{
		_images.push_back( Texture { image, Texture::Desc { format, Texture::Usage::COLOR_ATTACHMENT, _device->_extent } } );
		_image_views.push_back( _device->create_texture_view( _images.back(), TextureView::Aspect::COLOR ) );
	}
}

std::tuple<uint32_t, renderer::Texture, renderer::TextureView> renderer::Swapchain::acquire()
{
	OPTICK_EVENT();
	const auto frame_index = _frame_count % MAX_FRAMES_IN_FLIGHT;
	if ( const auto result = _device->_device.waitForFences( *_frame_fences[ frame_index ], vk::True, UINT64_MAX );
		 result != vk::Result::eSuccess )
	{
		throw Error( "Render fence not signaled", result );
	}
	_device->reset_fences( { *_frame_fences[ frame_index ] } );

	const auto [ result, image_index ] = _swapchain.acquireNextImage( UINT64_MAX, _acquire_semaphores[ frame_index ] );
	if ( result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR )
	{
		throw Error( "Failed to acquire swapchain image", result );
	}
	_current_image = image_index;
	return { frame_index, _images[ image_index ], _image_views[ image_index ] };
}

void renderer::Swapchain::submit( CommandBuffer& buffer )
{
	const auto frame_index = _frame_count % MAX_FRAMES_IN_FLIGHT;
	const vk::CommandBufferSubmitInfo cmd_submit_info { .commandBuffer = buffer._cmd_buffer };
	const vk::SemaphoreSubmitInfo wait_info { .semaphore = _acquire_semaphores[ frame_index ],
											  .value = 1,
											  .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput };
	const vk::SemaphoreSubmitInfo signal_info { .semaphore = _submit_semaphores[ _current_image ],
												.value = 1,
												.stageMask = vk::PipelineStageFlagBits2::eAllGraphics };

	_device->_gfx_queue.submit2( vk::SubmitInfo2 { .waitSemaphoreInfoCount = 1,
												   .pWaitSemaphoreInfos = &wait_info,
												   .commandBufferInfoCount = 1,
												   .pCommandBufferInfos = &cmd_submit_info,
												   .signalSemaphoreInfoCount = 1,
												   .pSignalSemaphoreInfos = &signal_info },
								 _frame_fences[ frame_index ] );
}

void renderer::Swapchain::present()
{
	OPTICK_EVENT();
#ifdef USE_OPTICK
	::Optick::GpuFlip( static_cast<VkSwapchainKHR>( *_swapchain ) );
#endif

	const auto result = _device->_gfx_queue.presentKHR( vk::PresentInfoKHR { .waitSemaphoreCount = 1,
																			 .pWaitSemaphores = &*_submit_semaphores[ _current_image ],
																			 .swapchainCount = 1,
																			 .pSwapchains = &*_swapchain,
																			 .pImageIndices = &_current_image } );
	_device->notify_present();
	++_frame_count;
	if ( result != vk::Result::eSuccess )
	{
		throw Error( "Failed to present swapchain", result );
	}
}
