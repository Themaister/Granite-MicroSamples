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

static const uint32_t gbuffer_vert[] =
#include "shaders/gbuffer.vert.inc"
;

static const uint32_t gbuffer_frag[] =
#include "shaders/gbuffer.frag.inc"
;

static const uint32_t lighting_vert[] =
#include "shaders/lighting.vert.inc"
;

static const uint32_t lighting_frag[] =
#include "shaders/lighting.frag.inc"
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

	Vulkan::Program *gbuffer_prog = device.request_program(
			device.request_shader(gbuffer_vert, sizeof(gbuffer_vert)),
			device.request_shader(gbuffer_frag, sizeof(gbuffer_frag)));

	Vulkan::Program *lighting_prog = device.request_program(
			device.request_shader(lighting_vert, sizeof(lighting_vert)),
			device.request_shader(lighting_frag, sizeof(lighting_frag)));

	while (platform.is_alive)
	{
		wsi.begin_frame();

		auto cmd = device.request_command_buffer();

		// Here we're exploring the deep support for render passes in Granite.
		// Almost all older engines completely neglect this aspect because render passes did not exist in older APIs.
		// Re-architecting an entire API abstraction to support render passes is no small task.

		// Here we have a toy deferred renderer expressed with Vulkan multipass.
		// This setup is very explicit and it's something we need to take advantage of tile-based renderers.
		// The gain from doing it like this is dubious on desktop with current architectures,
		// but getting optimal performance on all architectures is a huge win.

		// Granite is quite low-level here, but there are some huge convenience points we cannot overlook:
		// - VkSubpassDependencies are set up automatically by analyzing the use of resources.
		// - Appropriate image layouts are set automatically based on resource use.
		// - VkAttachmentReferences are set automatically.
		// It is up to user to respect these layouts as initialLayout and finalLayout are deduced from how attachments are used,
		// but here we are using just WSI and transient images. These images are synchronized automatically with
		// VK_SUBPASS_EXTERNAL because initialLayout is UNDEFINED for all these attachments.
		// - VkRenderPass handles are created automatically based on ... you guessed it, hashing.
		Vulkan::RenderPassInfo rp;
		rp.num_color_attachments = 3;
		rp.color_attachments[0] = &device.get_swapchain_view();

		// If we can throw away the resources, we can just request on-demand transient images.
		// On tile-based, we don't need to spend physical memory for these attachments.
		// Transient attachments are naturally throw-away and reuseable,
		// so I felt it made sense to do synchronization automatically for these resources.

		// Fortunately, we can trivially do this using EXTERNAL subpass dependencies, so there is
		// zero tracking required to implement this.
		// This is probably the only case where I find EXTERNAL subpass dependencies useful ...
		// Transient images are kept around and are deallocated if not used again in a few frames.
		// You can create these images yourself as well using the ImageDomain::Transient mode.

		// This automatic synchronization is theoretically inoptimal on the GPU, since we might emit
		// redundant barriers. There might be room to enable "explicit sync" for transient attachments as well,
		// particularly in the render graph.
		rp.color_attachments[1] = &device.get_transient_attachment(
				device.get_swapchain_view().get_image().get_width(),
				device.get_swapchain_view().get_image().get_height(),
				VK_FORMAT_R8G8B8A8_UNORM,
				0);
		rp.color_attachments[2] = &device.get_transient_attachment(
				device.get_swapchain_view().get_image().get_width(),
				device.get_swapchain_view().get_image().get_height(),
				VK_FORMAT_R8G8B8A8_UNORM,
				1);

		// Depth format support varies across devices, so there's a generic "default depth" or depth-stencil format
		// which is either D24 or D32F depending on hardware.
		rp.depth_stencil = &device.get_transient_attachment(
				device.get_swapchain_view().get_image().get_width(),
				device.get_swapchain_view().get_image().get_height(),
				device.get_default_depth_format());

		// Explicit store, load and clear, the way it should be.
		// This is also very important for tile-based GPUs.
		// It can have an effect on desktop as well I've found, particularly depth buffers.
		// No flags set for an attachment will map to DONT_CARE.
		rp.store_attachments = 1 << 0;
		rp.clear_attachments = (1 << 0) | (1 << 1) | (1 << 2);
		rp.op_flags = Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;

		// We don't *NEED* to provide this. If we don't we get one subpass which is setup in an obvious way based on
		// the RenderPassInfo.
		Vulkan::RenderPassInfo::Subpass subpasses[2];
		rp.num_subpasses = 2;
		rp.subpasses = subpasses;

		// Fiddle with clear depth.
		rp.clear_depth_stencil.depth = 1.0f;

		// Pretend attachment 1 and 2 represent our "gbuffer".
		subpasses[0].num_color_attachments = 2;
		subpasses[0].color_attachments[0] = 1;
		subpasses[0].color_attachments[1] = 2;

		// We can control which layout the depth buffer is in.
		subpasses[0].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadWrite;

		// For the second subpass, we're going to do a fake "lighting" pass.
		// We pull in the two color attachments from last subpass as input attachments.
		// We can also pull in the depth buffer as an input attachment.
		// It is also possible to have a "feedback" where attachments are used as both input attachments and color/depth.
		// This triggers GENERAL image layouts and access must be manually synchronized with CommandBuffer::pixel_barrier().
		subpasses[1].num_color_attachments = 1;
		subpasses[1].color_attachments[0] = 0;
		subpasses[1].num_input_attachments = 3;
		subpasses[1].input_attachments[0] = 1;
		subpasses[1].input_attachments[1] = 2;
		subpasses[1].input_attachments[2] = 3; // Depth attachment is index rp.num_color_attachments.
		// ReadOnly depth, so we can use DEPTH_STENCIL_READ_ONLY_OPTIMAL (depth read-only + input attachment layout).
		subpasses[1].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadOnly;

		// Here we lazily create a VkRenderPass as well as a "compatible" VkRenderPass (used by VkFramebuffer and VkPipeline).
		// Granite supplies the attachments inline as we see here, but in Vulkan we need to create VkFramebuffer objects.
		// Ideally, Vulkan would not require this object ...
		// These framebuffers are also created on-demand and destroyed if not used in a few frames.
		// We use the temporary hashmap data structure here as well, similar to descriptor set management.
		cmd->begin_render_pass(rp);
		{
			cmd->set_opaque_state();
			cmd->set_program(gbuffer_prog);
			// Fill the two gbuffers with red and blue color, see shaders/gbuffer.frag.
			// The vertex shader generates a quad, so no VBO needed.
			cmd->draw(3);
		}
		cmd->next_subpass();
		{
			cmd->set_opaque_state();
			cmd->set_program(lighting_prog);
			// Need to turn off depth writes, but keeps test enabled.
			// This is what a typical deferred renderer would do.
			cmd->set_depth_test(true, false);
			// Pulls out the input attachment views from the frame buffer and binds them to (0, 0), (0, 1) and (0, 2).
			cmd->set_input_attachments(0, 0);
			// This shaders adds the two gbuffer attachments together and multiplies with the depth,
			// which gives a dark magenta color.
			cmd->draw(3);
		}
		cmd->end_render_pass();
		device.submit(cmd);
		wsi.end_frame();
	}

	return true;
}

int main()
{
	// Copy-pastaed from sample 06.
	SDL_Window *window = SDL_CreateWindow("08-render-passes", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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
