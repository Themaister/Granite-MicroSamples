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

static const uint32_t triangle_vert[] =
#include "shaders/triangle.vert.inc"
;

static const uint32_t triangle_frag[] =
#include "shaders/triangle.frag.inc"
;

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

	Vulkan::Program *prog = device.request_program(
			device.request_shader(triangle_vert, sizeof(triangle_vert)),
			device.request_shader(triangle_frag, sizeof(triangle_frag)));

	while (platform.is_alive)
	{
		wsi.begin_frame();

		{
			auto cmd = device.request_command_buffer();

			// See sample 06.
			Vulkan::RenderPassInfo rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
			rp.clear_color[0].float32[0] = 0.1f;
			rp.clear_color[0].float32[1] = 0.2f;
			rp.clear_color[0].float32[2] = 0.3f;
			cmd->begin_render_pass(rp);

			// There are certain kinds of data which you often want to just allocate, use, and throw away.
			// In rendering engines, this concept is so common that the linear allocator is a fundamental allocator.
			// Other names include:
			// - Scratch allocator
			// - Chain allocator
			// The characteristics of data which are suited for linear allocators are:
			// - Allocations never need to be freed individually.
			// - Lifetime is only needed for an instant.
			// - Allocations are small and frequent.

			// Granite command buffers can allocate scratch memory very efficiently for:
			// - Vertex buffer data (CPU particle systems is a great example here)
			// - Index buffer data (why not)
			// - Uniform buffer data (extremely useful)
			// - General staging data for in-VRAM texture updates.

			// These allocations are backed by a pool of buffers.
			// Each buffer has a fixed size which depends on their type.
			// One the buffer is exhausted, it is placed in the frame context (see sample 03)
			// to be recycled one the frame is complete.
			// Typically the buffer is just HOST_VISIBLE, so we do not need anything,
			// but Granite also supports a code path where we have a CPU side and GPU side buffer
			// which needs to be copied on the DMA queue when submitting command buffers.
			// I never found any gain from doing that, and letting GPU cache source read-only data over PCI
			// on-demand works just fine.

			// Each command buffer owns a buffer at a time, and allocations are completely lock-free.

			// Here we do a lot of stuff in one call:
			// Allocate N bytes of data from a linear allocator (ultra-cheap).
			// Bind the index buffer as a 16-bit index buffer.
			// Returns the host pointer which user will write into.
			// This pointer points straight to a persistently mapped VkBuffer,
			// and we just need to write the data before the command buffer
			// is submitted. At that time, the pointer is invalid.

			// Using an index buffer here to draw a quad is rather silly, this is a demo ;)
			const uint16_t indices[6] = { 0, 1, 2, 3, 2, 1 };
			void *indices_data = cmd->allocate_index_data(sizeof(indices), VK_INDEX_TYPE_UINT16);
			memcpy(indices_data, indices, sizeof(indices));

			const float positions[4 * 3] = {
				-0.5f, -0.5f, 0.0f,
				-0.5f, +0.5f, 0.0f,
				+0.5f, -0.5f, 0.0f,
				+0.5f, +0.5f, 0.0f,
			};

			const float colors[4 * 4] = {
				1.0f, 0.0f, 0.0f, 1.0f,
				0.0f, 1.0f, 0.0f, 1.0f,
				0.0f, 0.0f, 1.0f, 1.0f,
				1.0f, 1.0f, 1.0f, 1.0f,
			};

			// Same as for index data here.
			// Vertex data is bound in buffer binding slots.
			// Vulkan has a concept for buffer bindings and attributes which refer to the buffers.
			// Granite retains the same system.
			// Allocate vertex data, bind the buffer
			void *position_data = cmd->allocate_vertex_data(
					0 /*binding*/,
					sizeof(positions) /*size to allocate*/,
					3 * sizeof(float) /*stride*/);

			void *color_data = cmd->allocate_vertex_data(
					1 /*binding*/,
					sizeof(colors) /*size to allocate*/,
					4 * sizeof(float) /*stride*/);

			memcpy(position_data, positions, sizeof(positions));
			memcpy(color_data, colors, sizeof(colors));

			cmd->set_vertex_attrib(
					0 /*attribute*/,
					0 /*binding*/,
					VK_FORMAT_R32G32B32_SFLOAT, /*format*/
					0 /*offset*/);
			cmd->set_vertex_attrib(
					1 /*attribute*/,
					1 /*binding*/,
					VK_FORMAT_R32G32B32A32_SFLOAT, /*format*/
					0 /*offset*/);

			// The most useful allocator, the uniform buffer allocator.
			// We allocate data, binding the buffer to designated set/binding,
			// and get a pointer where we can fill in UBO data.
			// There is a convenience templated member function which returns
			// T* rather than having to deal with void* and computing size in bytes.

			// see shaders/triangle.vert
			struct VertexUBO
			{
				float offset[2];
				float scale[2];
			};
			VertexUBO *vert_ubo = cmd->allocate_typed_constant_data<VertexUBO>(
					0, /* set */
					0, /* binding */
					1); /* count */

			// Shift the triangle a bit off-center.
			vert_ubo->offset[0] = 0.2f;
			vert_ubo->offset[1] = 0.2f;
			vert_ubo->scale[0] = 1.5f;
			vert_ubo->scale[1] = 1.5f;

			// see shaders/triangle.frag
			struct FragmentUBO
			{
				float color_mod[4];
			};
			FragmentUBO *frag_ubo = cmd->allocate_typed_constant_data<FragmentUBO>(
					0, /* set */
					1, /* binding */
					1); /* count */
			frag_ubo->color_mod[0] = 2.0f;
			frag_ubo->color_mod[1] = 1.0f;
			frag_ubo->color_mod[2] = 0.5f;
			frag_ubo->color_mod[3] = 0.25f;

			cmd->set_program(prog);

			// So much going on here, that's for another sample ...
			cmd->set_opaque_state();
			cmd->draw_indexed(6);

			cmd->end_render_pass();

			// All the internal buffers allocated for
			// the various allocators are now considered "dead", and will be recycled
			// when this frame completes.
			device.submit(cmd);
		}

		wsi.end_frame();
	}

	return true;
}

int main()
{
	// Copy-pastaed from sample 06.
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
