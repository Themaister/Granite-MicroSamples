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

// See sample 06 for details.
struct SDL2Platform : Vulkan::WSIPlatform
{
	explicit SDL2Platform(SDL_Window *window_)
		: window(window_)
	{
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkSurfaceKHR surface;
		if (SDL_Vulkan_CreateSurface(window, instance, &surface))
			return surface;
		else
			return VK_NULL_HANDLE;
	}

	std::vector<const char *> get_instance_extensions() override
	{
		unsigned instance_ext_count = 0;
		SDL_Vulkan_GetInstanceExtensions(window, &instance_ext_count, nullptr);
		std::vector<const char *> instance_names(instance_ext_count);
		SDL_Vulkan_GetInstanceExtensions(window, &instance_ext_count, instance_names.data());
		return instance_names;
	}

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
		return is_alive;
	}

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
	// Copy-pastaed from sample 06.
	SDL2Platform platform(window);

	Vulkan::WSI wsi;
	wsi.set_platform(&platform);
	wsi.set_backbuffer_srgb(true); // Always choose SRGB backbuffer formats over UNORM. Can be toggled in run-time.
	if (!wsi.init(1 /*num_thread_indices*/))
		return false;

	Vulkan::Device &device = wsi.get_device();

	while (platform.is_alive)
	{
		wsi.begin_frame();
		auto cmd = device.request_command_buffer();
		cmd->begin_render_pass(rp);
		cmd->end_render_pass();
		device.submit(cmd);
		wsi.end_frame();
	}

	return true;
}

int main()
{
	// Copy-pastaed from sample 06.
	SDL_Window *window = SDL_CreateWindow("09-synchronization", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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
