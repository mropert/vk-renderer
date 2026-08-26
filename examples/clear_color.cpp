#include <SDL3/SDL.h>
#include <renderer/command_buffer.h>
#include <renderer/device.h>
#include <renderer/swapchain.h>

#include <renderer/details/profiler.h>

int main()
{
	renderer::Device device( "clear_color" );
	renderer::Swapchain swapchain( device, renderer::Texture::Format::R8G8B8A8_SRGB );

	bool quit = false;

	while ( !quit )
	{
		SDL_Event event;
		while ( SDL_PollEvent( &event ) )
		{
			if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
				 || ( event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE ) )
			{
				quit = true;
			}
		}

		auto [ image_index, swapchain_image, swapchain_image_view ] = swapchain.acquire();
		auto command_buffer = device.grab_command_buffer();

		command_buffer->reset();
		command_buffer->begin();
		command_buffer->texture_barrier( swapchain_image,
											renderer::ResourceState::UNDEFINED,
											renderer::ResourceState::COLOR_ATTACHMENT );
		command_buffer->begin_rendering(
			device.get_extent(),
			renderer::RenderAttachment { .target = swapchain_image_view, .clear_value = { { 1.f, 0.f, 1.f, 1.f } } },
			renderer::RenderAttachment {} );
		command_buffer->end_rendering();
		command_buffer->texture_barrier( swapchain_image,
											renderer::ResourceState::COLOR_ATTACHMENT,
											renderer::ResourceState::PRESENT );
		command_buffer->end();

		swapchain.submit( *command_buffer );
		swapchain.present();
	}
	// Wait idle before we start running the destructors
	device.wait_idle();

	PROFILER_SHUTDOWN();
}
