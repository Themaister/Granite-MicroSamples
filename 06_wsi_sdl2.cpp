/* Copyright (c) 2019 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vulkan.hpp"
#include "device.hpp"
#include "wsi.hpp"
#include "util.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

// A sample which targets SDL2.

// We use an abstract interface to create VkSurfaceKHR, query which extensions are required to create
// instances and devices, and lets the WSI code query if the surface is about to be destroyed.
// The interface is generally used in WSI::init().

// These interfaces are generally never seen by users in the full Granite codebase as this is further abstracted
// behind the Granite::Application interface.

// As long as you are able to create a VkSurfaceKHR, the WSI interface can be used.
// Vulkan::WSI can also be used with externally created images (WSI::init_external_swapchain) with user created
// acquire and release semaphores. This is used for platforms like headless or libretro
// where we have a virtual swapchain and intend for the "swapchain" to be sampled from later.

// There is also a GLFW implementation of this interface in the Granite code base which is far more complete.
struct SDL2Platform : Vulkan::WSIPlatform
{
	explicit SDL2Platform(SDL_Window *window_)
		: window(window_)
	{
	}

	// SDL2 and GLFW have functions to create surfaces in a generic way.
	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkSurfaceKHR surface;
		if (SDL_Vulkan_CreateSurface(window, instance, &surface))
			return surface;
		else
			return VK_NULL_HANDLE;
	}

	// We'll need VK_KHR_surface and whatever platform extension we need.
	// SDL2 and GLFW abstracts this.
	std::vector<const char *> get_instance_extensions() override
	{
		unsigned instance_ext_count = 0;
		SDL_Vulkan_GetInstanceExtensions(window, &instance_ext_count, nullptr);
		std::vector<const char *> instance_names(instance_ext_count);
		SDL_Vulkan_GetInstanceExtensions(window, &instance_ext_count, instance_names.data());
		return instance_names;
	}

	// When creating a surface and swapchain, we need to know the dimensions.
	// Usually however, the Window itself will force the native 1:1 pixel size anyways, but certain platforms
	// like KHR_display and Android would be able to scale from this size as requested.
	uint32_t get_surface_width() override
	{
		int w, h;
		SDL_Vulkan_GetDrawableSize(window, &w, &h);
		return w;
	}

	uint32_t get_surface_height() override
	{
		int w, h;
		SDL_Vulkan_GetDrawableSize(window, &w, &h);
		return h;
	}

	bool alive(Vulkan::WSI &) override
	{
		// This is generally only relevant for Granite::Application.
		return is_alive;
	}

	// The poll input is called at a strategic time.
	// Here we poll platform events and handle any relevant events.
	void poll_input() override
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				is_alive = false;
				break;

			default:
				break;
			}
		}
	}

	SDL_Window *window;
	bool is_alive = true;
};

static bool run_application(SDL_Window *window)
{
	SDL2Platform platform(window);

	// The WSI object in Granite has its own Vulkan::Context and Vulkan::Device.
	// It's also responsible for creating a swapchain, surface and pumping the presentation loop.
	Vulkan::WSI wsi;
	wsi.set_platform(&platform);
	wsi.set_backbuffer_srgb(true); // Always choose SRGB backbuffer formats over UNORM. Can be toggled in run-time.
	if (!wsi.init(1 /*num_thread_indices*/))
		return false;

	Vulkan::Device &device = wsi.get_device();

	// platform.is_alive is set to false in SDL2Platform::poll_input() when killing the window.
	while (platform.is_alive)
	{
		// Beginning a frame means:
		// - vkAcquireNextImageKHR is called if no image is currently acquired.
		// - Device::next_frame_context() is called.
		// - Synchronization for the WSI image with semaphores is set up internally.
		// - poll_input() is called after AcquireNextImageKHR (since acquire can block, we want to poll input as late as possible).
		wsi.begin_frame();

		{
			auto cmd = device.request_command_buffer();

			// Just render a clear color to screen.
			// There is a lot of stuff going on in these few calls which will need its own sample to explore w.r.t. synchronization.
			// For now, you'll just get a blue-ish color on screen.
			Vulkan::RenderPassInfo rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
			rp.clear_color[0].float32[0] = 0.1f;
			rp.clear_color[0].float32[1] = 0.2f;
			rp.clear_color[0].float32[2] = 0.3f;
			cmd->begin_render_pass(rp);
			cmd->end_render_pass();
			device.submit(cmd);
		}

		// Ending a frame will trigger a vkQueuePresentKHR if the swapchain image was rendered to.
		// The semaphores are also handled implicitly.
		// Generally, the WSI images in Granite get a lot of special treatment since it's trivial to track state related
		// to WSI images compared to arbitrary images.
		wsi.end_frame();
	}

	return true;
}

int main()
{
	SDL_Window *window = SDL_CreateWindow("06-wsi-sdl2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                                      640, 360,
	                                      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		LOGE("Failed to create SDL window!\n");
		return 1;
	}

	// Init loader with GetProcAddr directly from SDL2 rather than letting Granite load the Vulkan loader.
	if (!Vulkan::Context::init_loader((PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr()))
	{
		LOGE("Failed to create loader!\n");
		return 1;
	}

	if (!run_application(window))
	{
		LOGE("Failed to run application.\n");
		return 1;
	}

	SDL_DestroyWindow(window);
	SDL_Vulkan_UnloadLibrary();
}
