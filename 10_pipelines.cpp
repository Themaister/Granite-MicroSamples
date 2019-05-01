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
		auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.3f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.1f;
		cmd->begin_render_pass(rp);

		// In terms of managing render state, Granite is rather old school.
		// The main reason I think that old school render state models of "set this state, set that state" is discouraged is
		// that all the state is global and any state set in one part of the application will leak to all future
		// uses of the context. This is deeply problematic because it's very hard to reason about which state the context is
		// in at any point, and the way this is usually resolved is to have extra layers of state tracking on top
		// of the API you're using. This is just silly.
		// It also means middle-ware is complicated since it might need to modify global state and we have to deal with that somehow.

		// Granite improves on this situation in major ways:
		// - Render state is local to every command buffer. This is kind of obvious for Vulkan since we have command buffers,
		//   but it fixes a major problem in that it's now very easy to reason about render state.
		//   We know we are never going to leak state in the way older APIs did.
		// - There are functions to reset all render state to a known "common case" state.
		// - We can save and restore render state we are interested in.

		// Resets all render state to a known state. This is the common render state which renders triangle lists with depth testing.
		cmd->set_opaque_state();

		Vulkan::CommandBufferSavedState saved;

		// Save all possible state to a blob. It can be restored as many times as desired, so this is suitable for implementing
		// a state "stack" if desired.
		// This is very useful I find in the high-level renderer interface in Granite.
		// At a top-level we can set the "default" render state we expect from a depth-only pass, opaque pass, transparency pass, etc.
		// This can be considered "global state" for the render pass.
		// That state is saved, and when rendering individual objects they can override the state if desired, but usually they don't need to.
		// They only tend to modify the shaders and bindings. The "global" state can be restored between draws.
		cmd->save_state(Vulkan::COMMAND_BUFFER_SAVED_RENDER_STATE_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_BINDINGS_0_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_BINDINGS_1_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_BINDINGS_2_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_BINDINGS_3_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_PUSH_CONSTANT_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_SCISSOR_BIT |
		                Vulkan::COMMAND_BUFFER_SAVED_VIEWPORT_BIT, saved);

		// Setting some random static state.
		cmd->set_depth_test(true, true);
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		cmd->set_primitive_restart(true);
		cmd->set_depth_bias(true);
		cmd->set_depth_compare(VK_COMPARE_OP_EQUAL);
		cmd->set_stencil_test(true);
		cmd->set_stencil_ops(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT);
		cmd->set_color_write_mask(0xe);

		// This is *potentially* static state. It only participates if the shader program uses these spec constants.
		cmd->set_specialization_constant_mask(0xf);
		cmd->set_specialization_constant(0, 1.0f);
		cmd->set_specialization_constant(1, 2.0f);
		cmd->set_specialization_constant(2, 3.0f);
		cmd->set_specialization_constant(3, 4.0f);

		// Stride and input rate affects pipeline compilation.
		// Vertex attributes as well.
		cmd->allocate_vertex_data(0 /* binding */, 16, 4 /* stride */, VK_VERTEX_INPUT_RATE_VERTEX);
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8B8A8_UNORM, 0);

		// Set some dynamic state. This is flushed out as necessary, does not affect pipeline compilation.
		cmd->set_depth_bias(1.0f, 1.0f);
		cmd->set_stencil_front_reference(1, 2, 4);

		VkViewport vp = { 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f };
		cmd->set_viewport(vp);
		VkRect2D rect = { { 0, 0 }, { 4, 4 } };
		cmd->set_scissor(rect);

		// Restore the state back to what it was.
		cmd->restore_state(saved);

		// Pipelines are lazily created.
		// If we haven't seen the pipeline before, this is a problem since vkCreate*Pipeline can be very expensive,
		// which causes stuttering.
		//
		// The mechanism in Granite to pre-warm the internal cache is using Fossilize, but it's of course also possible
		// to record a command buffer which only is designed to warm up caches.
		// We do pay the cost of some extra CPU work to hash render state, but I haven't seen any problem with it.
		//
		// We haven't bound a program here, so the draw call is simply going to be dropped.

		// Overall, this is quite GL-esque, but I kinda like it actually. I'm not a big fan of being responsible for filling out
		// massive state structures manually and managing them.

		// Ideally, we would be able to just bind a VkPipeline directly and never deal with render state directly, but it is problematic.
		// I don't think this style can be used outside a game engine with a strong asset pipeline which can bake all known uses up-front
		// and all pipeline assignments are known.
		// We need to know a lot of state up front, and there is a lot of coupling between modules in a renderer to make this work.
		// - The shader modules (well, duh).
		// - Vertex buffer layouts (strides and attribute formats). This is usually inferred, and needs to be solved with "standardized" vertex buffer layouts.
		// - The render pass, and which subpass the pipeline is used in. This one can be very problematic for graphics.
		//   There's no reason why a shader cannot be used in multiple scenarios where we have different render target formats.
		//   For rendering normal meshes there might be rendering with MSAA off/2x/4x/8x, depth-only rendering, FP16 HDR vs sRGB LDR.
		//   A common problem when baking stuff up front is the combinatorial explosion we end up with.
		// - High level render state. Is depth bias on? Opaque vs transparency pass? Depth writes on or off?
		// - Specialization constants, who controls it?

		// It's certainly not impossible, and people have done so,
		// but it's not going to work for certain use cases like emulators for example where we cannot control what the application needs to do.
		// I don't want my API design to be tied to a very particular asset pipeline.
		// At the end of the day, working in terms of pipelines directly will only give some CPU improvements at the cost of flexibility.
		// I haven't found a case where this matters yet, but it might count in AAA engines with tens of thousand pipelines flying around.
		// This is why it's impossible to design a one-size-fits-all graphics abstraction.
		// There's always going to be trade-offs which some use cases cannot accept.

		cmd->draw(0);

		// It might be possible for Granite to grab the VkPipeline for a particular render state here,
		// and then support just binding that later. I haven't found the need for it, but it's not impossible.
		// A hybrid solution might be nice, perhaps.

		cmd->end_render_pass();
		device.submit(cmd);
		wsi.end_frame();
	}

	return true;
}

int main()
{
	// Copy-pastaed from sample 06.
	SDL_Window *window = SDL_CreateWindow("10-pipelines", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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