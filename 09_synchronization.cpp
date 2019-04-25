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

	// In this sample we are going to render to an off-screen surface in the graphics queue,
	// copy it back to the user in the transfer/DMA queue and read the results.
	// NOTE: This is a pretty ridiculous way to use multiple queues in Vulkan, but this is the shortest example I can
	// think of where we demonstrate barriers, readbacks, image layouts, semaphores and fences.

	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
	rt_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	// This controls if we have EXCLUSIVE queue family or CONCURRENT queue family sharing.
	// In Vulkan, we can get a theoretical gain by exclusively handing off ownership between queues, but the easy way is to declare up front
	// that we're going to use this image by both without having to mess around with ownership transfers.
	rt_info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT | Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT;

	Vulkan::BufferCreateInfo buffer_readback_info;
	buffer_readback_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	// We're going to read from this buffer on CPU, so better make sure it's a CACHED pointer!
	buffer_readback_info.domain = Vulkan::BufferDomain::CachedHost;
	buffer_readback_info.size = 4 * 4 * sizeof(uint32_t);

	while (platform.is_alive)
	{
		wsi.begin_frame();
		auto graphics_cmd = device.request_command_buffer(Vulkan::CommandBuffer::Type::Generic);

		// Now we're starting to see manual synchronization come into play.
		// This image is neither a WSI image nor a transient image. It is fully under our management, and Granite will
		// not do any hand-holding here. Automatically dealing with synchronization in Vulkan is so invasive and places
		// such a large burden on the implementation that I don't think a middle-level abstraction should do it.
		// To automate this process, a render graph or similar is a far more suitable option since we can know the synchronization required early,
		// rather than require the implementation to observe usage in the last minute and perform the correct checks at the last minute.
		// To fully automate synchronization and image layouts is a key aspect of a high-level abstraction to me, like GL and D3D11.
		// Granite only automates synchronization where it's trivial to do so, and where it requires no complicated tracking.

		// Image layouts for non-WSI and non-transient resources must always be in the appropriate Vulkan image layout when executing a command.
		// Each image can be in either its Optimal (context dependent) or General (GENERAL) layouts. With Optimal, the optimal layout for the use is assumed
		// and it's up to the user to use the right layout, e.g. when used in a render pass as a color attachment, COLOR_ATTACHMENT_OPTIMAL is assumed,
		// as a read-only texture, SHADER_READ_ONLY_OPTIMAL, etc. Vulkan image layouts generally work like this where there is one "optimal" one and one "generic" option.
		// The only real exception to this rule is with depth buffers, but we make use of the render pass information to pick correct layouts in this case,
		// since this case only applies to depth attachments and input attachments.
		// The General layout always assumes GENERAL image layout. This is useful for image load/store images for example.

		// We create a new image here every frame to break the "bubble" of ping-ponging the image between transfer and graphics queues.
		Vulkan::ImageHandle rt = device.create_image(rt_info);

		// Optimal is the default which should be used in almost all cases, this line is just for illustration.
		rt->set_layout(Vulkan::Layout::Optimal);

		// This translates directly to vkCmdPipelineBarrier with a VkImageMemoryBarrier.
		// This image is fresh, so just wait for TOP_OF_PIPE_BIT (i.e. don't wait at all).
		graphics_cmd->image_barrier(*rt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		// There are many variants of barriers in Vulkan::CommandBuffer.
		// It's possible to use the "raw" interfaces for purposes of batching image barriers for example.
		// Those map 1:1 to vkCmdPipelineBarrier.
		// In this sample we'll only use the basic barrier interfaces.

		Vulkan::RenderPassInfo rp;
		rp.num_color_attachments = 1;
		rp.color_attachments[0] = &rt->get_view();
		rp.store_attachments = 1 << 0;
		rp.clear_attachments = 1 << 0;

		// Clear to magenta.
		rp.clear_color[0].float32[0] = 1.0f;
		rp.clear_color[0].float32[1] = 0.0f;
		rp.clear_color[0].float32[2] = 1.0f;
		rp.clear_color[0].float32[3] = 0.0f;

		// In this render pass, initialLayout is COLOR_ATTACHMENT_OPTIMAL and finalLayout is COLOR_ATTACHMENT_OPTIMAL.
		// With WSI images for example, all the layout gunk is automatic, with initial = UNDEFINED, and final = PRESENT_SRC_KHR.
		// And the barrier would be automatic through the use of VK_SUBPASS_EXTERNAL dependencies.
		// In this scenario, we're on our own however.
		graphics_cmd->begin_render_pass(rp);
		// Clear the top-left pixel to green, because why not :)
		VkClearRect clear_rect = {};
		clear_rect.layerCount = 1;
		clear_rect.rect.extent.width = 1;
		clear_rect.rect.extent.height = 1;
		VkClearValue clear_value = {};
		clear_value.color.float32[1] = 1.0f;

		// This is the render pass variant of clear image, not outside render pass one.
		graphics_cmd->clear_quad(0, clear_rect, clear_value, VK_IMAGE_ASPECT_COLOR_BIT);
		graphics_cmd->end_render_pass();

		// Let's transition this image to TRANSFER_SRC before we give it away to the transfer queue.
		// We use dstStageMask = BOTTOM_OF_PIPE here since we're going to use semaphores to synchronize. No need to block stages in the graphics queue.
		// (Don't worry if this is confusing, this is pretty down in the abyss as far as Vulkan synchronization goes.)
		graphics_cmd->image_barrier(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

		// Here we're signalling a semaphore, so Granite will need to vkQueueSubmit right away instead of queueing up command buffers.
		Vulkan::Semaphore graphics_to_transfer_sem;
		device.submit(graphics_cmd, nullptr, 1, &graphics_to_transfer_sem);

		// Inject the semaphore in the transfer queue, where it should block the TRANSFER stage until we're done rendering.
		// We can only wait for a semaphore once. This can be a bit icky if you need to wait in multiple queues, hopefully we'll see some API improvements here.
		device.add_wait_semaphore(Vulkan::CommandBuffer::Type::AsyncTransfer, graphics_to_transfer_sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

		// Create a new buffer which we will copy the image to and readback on CPU asynchronously.
		auto buffer_readback = device.create_buffer(buffer_readback_info);
		auto transfer_cmd = device.request_command_buffer(Vulkan::CommandBuffer::Type::AsyncTransfer);
		transfer_cmd->copy_image_to_buffer(*buffer_readback, *rt, 0, {}, { 4, 4, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		// In order observe reads on the host, you have to do this memory barrier in Vulkan.
		transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                      VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		// Signal a manual fence. The fence will signal once the readback is complete, and then we can read back the data.
		// This is very straight forward.
		Vulkan::Fence readback_fence;
		device.submit(transfer_cmd, &readback_fence);

		// Just render something to the swapchain.
		graphics_cmd = device.request_command_buffer();
		rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		graphics_cmd->begin_render_pass(rp);
		graphics_cmd->end_render_pass();
		device.submit(graphics_cmd);

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
