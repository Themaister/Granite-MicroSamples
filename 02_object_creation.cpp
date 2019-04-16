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

Vulkan::BufferHandle create_buffer(Vulkan::Device &device)
{
	// Like raw Vulkan, we have creation structs, although they are default-initialized since this is C++.
	Vulkan::BufferCreateInfo info;

	// Size in bytes.
	info.size = 64;

	// The domain is where we want the buffer to live.
	// This abstracts the memory type jungle.
	// - Device is DEVICE_LOCAL. Use this for static buffers which are read from many times.
	// - Host is HOST_VISIBLE, but probably not CACHED. Use this for uploads.
	// - CachedHost is HOST_VISIBLE with CACHED. Used for readbacks.
	// - LinkedDeviceHost is a special one which is DEVICE_LOCAL and HOST_VISIBLE.
	//   This matches AMD's pinned 256 MB memory type. Not really used at the moment.
	info.domain = Vulkan::BufferDomain::Device;

	// Usage flags is as you expect. If initial copies are desired as well,
	// the backend will add in transfer usage flags as required.
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	// Misc creation flags which don't exist in Vulkan. It's possible to request the buffer to be cleared
	// on creation. For Device-only types, this means allocating a command buffer and submitting that.
	// Barriers are taken care of automatically.
	info.misc = 0;

	// Initial data can be passed in. The data is copied on the transfer queue and barriers are taken care of.
	// For more control, you can pass in nullptr here and deal with it manually.
	// If you're creating a lot of buffers with initial data in one go, it might makes sense to do the upload manually.
	const void *initial_data = nullptr;

	// Memory is allocated automatically.
	Vulkan::BufferHandle buffer = device.create_buffer(info, initial_data);
	return buffer;
}

Vulkan::ImageHandle create_image(Vulkan::Device &device)
{
	// immutable_2d_image sets up a create info struct which matches what we want.
	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8G8B8A8_UNORM);

	// We can use an initial layout here. If != UNDEFINED, we need to submit a command buffer with
	// the image barriers to transfer the image to our desired layout.
	// Mostly useful for read-only images which we only touch once from a synchronization point-of-view.
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Levels == 0 -> automatically deduce it.
	info.levels = 0;

	// We can request mips to be generated automatically.
	info.misc = Vulkan::IMAGE_MISC_GENERATE_MIPS_BIT;

	Vulkan::ImageInitialData initial_data = {};
	static const uint32_t checkerboard[] = {
		0u, ~0u, 0u, ~0u,
		~0u, 0u, ~0u, 0u,
		0u, ~0u, 0u, ~0u,
		~0u, 0u, ~0u, 0u,
	};
	initial_data.data = checkerboard;

	// Memory is allocated automatically.
	Vulkan::ImageHandle handle = device.create_image(info, &initial_data);
	return handle;
}

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

	// For resource handles, I went with a smart pointer, but not the standard ones.
	// Vulkan::BufferHandle -> Util::IntrusivePtr<Vulkan::Buffer>.
	// The main differences from a std::shared_ptr<> are:
	// - No weak pointer support.
	// - Ref-count can be atomic or non-atomic based on if we build with MT support or not.
	// - Ref-count block is always allocated with the object itself (intrusive part).
	// - Even without the RAII IntrusivePtr wrapper, it's possible to manually use the ref-counts.
	// - The handle pointers are allocated from an object pool.
	// In the asymptotic case creating resource handles will never need heap allocation or frees.
	// The handles are freed with special deleters which the intrusive pointers take care of.

	Vulkan::BufferHandle buffer = create_buffer(device);
	Vulkan::ImageHandle image = create_image(device);

	// In Vulkan you have to create an image view from a texture separately from the image.
	// In 99% of cases you use the "default" view, so Granite adds this convenience for you.
	// Vulkan::ImageView can contain multiple views to deal with render-to-texture of mipmapped images,
	// rendering to layers, etc. The right views are used depending which functions consume the Vulkan::ImageView.
	Vulkan::ImageView &view = image->get_view();

	// Suppress warning.
	(void)view;

	// All the objects will go out of scope here, and their memory are cleaned up.
}