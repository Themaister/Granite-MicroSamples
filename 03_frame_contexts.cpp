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
#include "util.hpp"

// See sample 02_object_creation.
Vulkan::BufferHandle create_buffer(Vulkan::Device &device)
{
	Vulkan::BufferCreateInfo info;
	info.size = 64;
	info.domain = Vulkan::BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.misc = 0;
	const void *initial_data = nullptr;
	Vulkan::BufferHandle buffer = device.create_buffer(info, initial_data);
	return buffer;
}
////

int main()
{
	// See sample 01.
	if (!Vulkan::Context::init_loader(nullptr))
	{
		LOGE("Failed to create loader!\n");
		return 1;
	}

	Vulkan::Context context;
	if (!context.init_instance_and_device(nullptr, 0, nullptr, 0))
	{
		LOGE("Failed to create VkInstance and VkDevice.\n");
		return 1;
	}

	Vulkan::Device device;
	device.set_context(context);
	////

	// This is done automatically for us in Device::set_context().
	// The default for desktop is 2 frame contexts, and 3 frame contexts on Android
	// (since TBDR renderers typically require a bit more buffering for optimal performance).
	// A frame context generally maps to an on-screen frame, but it does not have to.
	// In the earlier designs it used to map 1:1 to a WSI frame, but this got clumsy over time,
	// especially in headless operation.
	device.init_frame_contexts(2);

	// We start in frame context #0.
	// Each frame context has some state associated with it.
	// - Command pools are tied to a frame context.
	// - Queued up command buffers for submission.
	// - Objects which are pending to be destroyed.

	// Let's pretend we're doing this in the first frame.
	{
		// Command buffers are transient in Granite.
		// Once you request a command buffer you must submit it in the current frame context before moving to the next one.
		// More detailed examples of command buffers will follow in future samples.
		// There are different command buffer types which correspond to general purpose queue, async compute, DMA queue, etc.
		// Generic is the default, and the argument can be omitted.
		Vulkan::CommandBufferHandle cmd = device.request_command_buffer(Vulkan::CommandBuffer::Type::Generic);
		// Pretend we're doing some work here on the command buffer.

		// We're also creating a temporary buffer and destroying it this frame since it will go out of scope.
		Vulkan::BufferHandle buffer = create_buffer(device);

		// Submitting a command buffer simply queues it up. We will not call vkQueueSubmit and flush out all pending command buffers here unless:
		// - We need to signal a fence.
		// - We need to signal a semaphore.
		Vulkan::Fence *fence = nullptr;
		const unsigned num_semaphores = 0;
		Vulkan::Semaphore *semaphores = nullptr;

		// Command buffers must be submitted. Failure to do so will trip assertions in debug builds.
		device.submit(cmd, fence, num_semaphores, semaphores);

		// buffer (the CPU handle) will be destroyed here since it's going out of scope,
		// however, the VkBuffer inside it is a GPU resource, which might in theory be in use by the GPU.
		// We do *NOT* want to track when a buffer has been used and reclaim the resource as early as possible, since it's useless overhead.
		// Defer its destruction by appending the VkBuffer and its memory allocation to the current frame context.
		// This is a conservative approach which is deterministic and always works, but might hold on to GPU resources a little too long.
	}

	// Normally, if using the WSI module in Granite (to be introduced later), we don't need to iterate this ourselves since
	// this is called automatically on "QueuePresent". However, for headless operation like this,
	// we need to call this ourselves to mark when we have submitted enough work for the GPU.
	// If we have some pending work in the current frame context, this is flushed out.
	// Fences are signalled internally to keep track of all work that happened in this frame context.
	device.next_frame_context();

	// Now we're in frame context #1, and when starting a frame context, we need to wait for all pending fences associated with the context.
	{
		Vulkan::CommandBufferHandle cmd = device.request_command_buffer(Vulkan::CommandBuffer::Type::Generic);
		Vulkan::BufferHandle buffer = create_buffer(device);
		device.submit(cmd);
	}

	// Now we're back again in frame context #0, and any resources we used back in the first frame have now been reclaimed,
	// Command pools have been reset and we can reuse the old command buffers.
	// since we have waited for all command buffers which were ever submitted in that old frame context.
	// This is how we get double-buffering between CPU and GPU basically.
	device.next_frame_context();

	// This is the gist of Granite's lifetime handling. It defers deallocations until we know that any possible work is complete.
	// This is sub-optimal, but it is also 100% deterministic. This I believe is the right abstraction level for a "mid-level" implementation.
	// If you have one very long frame that is doing a lot of work and you're allocating and freeing memory a lot, you might end up with an OOM scenario.
	// To reclaim memory you must call Device::next_frame_context, or Device::wait_idle, which also immediately reclaims all memory and frees all pending resources.
	// Since we are resetting all command pools in wait_idle, all command buffers must have been submitted before calling this, similar to next_frame_context().
	device.wait_idle();
}
