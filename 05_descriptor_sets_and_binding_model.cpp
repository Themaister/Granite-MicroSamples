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

static const uint32_t simple_comp[] =
#include "shaders/simple.comp.inc"
;

// See 02_object_creation.
static Vulkan::BufferHandle create_ssbo(Vulkan::Device &device, const void *initial_data, VkDeviceSize size)
{
	Vulkan::BufferCreateInfo info;
	info.size = size;
	info.domain = Vulkan::BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	return device.create_buffer(info, initial_data);
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

	// Just like in 04_shaders_and_programs we request compute shaders like this.
	Vulkan::Shader *comp = device.request_shader(simple_comp, sizeof(simple_comp));
	Vulkan::Program *prog = device.request_program(comp);

	// Create some buffers with initial data. See shaders/simple.comp for what the shader is doing.
	float initial_a[64];
	float initial_b[64];
	for (unsigned i = 0; i < 64; i++)
	{
		initial_a[i] = float(i) + 2.0f;
		initial_b[i] = float(i) + 4.0f;
	}
	auto ssbo_a = create_ssbo(device, initial_a, sizeof(initial_a));
	auto ssbo_b = create_ssbo(device, initial_b, sizeof(initial_b));
	auto ssbo_out = create_ssbo(device, nullptr, sizeof(initial_a));

	auto cmd = device.request_command_buffer();

	// In Granite, the binding model is quite traditional.
	// This is likely the most high-level aspect of the backend.
	// This time around, we'll demonstrate GPU compute since there is a lot less state to deal with when
	// it comes to compute vs. graphics.

	// We bind programs to the command buffer.
	cmd->set_program(prog);

	// Granite has a desire to be portable to all kinds of devices, including mobile. Mobile GPUs tend to have a very old-school
	// kind of binding model, and in Granite I've chosen to expose the Vulkan's set/binding model directly to avoid as much friction as possible.
	// Rather than binding resources to a per-resource-type slot like in GL and D3D11, we are able to sort resource bindings by frequency here, where rarely updated resources are in low numbered sets,
	// and frequently updated sets are in higher numbered slots.
	// There are 4 descriptor sets, and 16 binding slots for each set, which matches the minimum spec for Vulkan.
	// For my use this is just fine. There is no impedance mismatch between the Vulkan API and Granite here, which is nice.

	// Descriptor sets are created in a lazy way. The pipeline layout knows all binding points which are active in the pipeline (see sample 04),
	// and we translate the bound resources into a VkDescriptorSet.
	// An important design consideration early was that descriptor sets are reused directly. If the same resources are bound every frame,
	// we actually do not need to update any descriptor sets, we simply pull it out of our DescriptorSetAllocator.
	// If the resources are not used for a while (8 frames), the VkDescriptorSet is recycled, and other sets can come in and overwrite the descriptors.
	// There is no need to free or allocate VkDescriptorSets this way. We can do this because we have completely separate descriptor set allocators for each unique descriptor set layout.
	// All sets which are allocated from their respective set allocators are fully compatible with each other.
	// This would not be the case if we had just one massive descriptor set pool with different set layouts being used all the time.
	// We would be forced to spend a lot more time rebuilding descriptor sets (or use push descriptors).
	// On some mobile GPUs, updating and allocating descriptor sets is rather slow and should be avoided. Not doing work is always nice.

	// In order to keep descriptor set allocator hit rate high, we can make sure that static resources and transient resources
	// are kept in separate sets when writing shaders.
	// The layout I typically use is:
	// - Set 0: Global uniform data (projection matrices and that kind of stuff)
	// - Set 1: Global texture resources (like shadow maps, etc)
	// - Set 2: Per-material data like textures.
	// - Set 3: Per-draw uniforms

	// Note that we do *NOT* take a reference-count on the individual resources here. Descriptor sets will eventually be recycled if bound resources are deleted,
	// since that descriptor set will never be used again and never get a chance to "refresh" itself.
	// All this logic is handled by the Util::TemporaryHashmap class which I'm quite happy with.

	// The downside of this model is of course extra CPU time spent in hashing and looking up, however I can't say I've seen this come up in a profile.
	// We must also consider the cycles we save by not updating descriptor sets all the time.
	cmd->set_storage_buffer(0 /*set*/, 0 /*binding*/, *ssbo_a);
	cmd->set_storage_buffer(0, 1, *ssbo_b);
	cmd->set_storage_buffer(1, 0, *ssbo_out);

	// Then there's a lot of variants for other resource types. SSBOs and compute are just the simplest things to show here.

	// Descriptor sets is probably the aspect of Vulkan where designs vary wildly across implementations.
	// I think there are two major alternatives:
	//
	// 1. Descriptor sets are explicitly created, and bound as one, inseparable unit.
	// Of course, it will seem redundant to bind resources and lazily translate that to descriptor sets, so why not just allocate a descriptor set
	// ahead of time persistently and just directly bound it? There are a few problems with it, which makes this idea annoying to execute in practice:
	// - We need to know and declare the target imageLayout of textures up front. This is obvious 99% of the time (e.g. a group of material textures which are SHADER_READ_ONLY_OPTIMAL),
	//   but in certain cases, especially with depth textures, things can get rather ambiguous.
	// - Some resources are completely transient in nature and it does not make sense to place them in persistent descriptor sets.
	//   The perfect example here is uniform buffers. In later samples, we'll look at the linear allocator system for transient data.
	// - Some resources depend on the frame buffer, i.e. input attachments. Baking descriptor sets for these resources is not obvious.
	// - We need to know the descriptor set layout (and by extension, the shaders as well) up-front. This is problematic if resources are to be used in more than one shader.
	//   The common antidote here is to settle on a "standard" pipeline layout so we can decouple shaders and resources.
	//   This means a lot of padding and redundant descriptor allocations instead.
	// - We have a limited amount of descriptor sets when targeting mobile (4). We do not have the luxury of splitting every individual "group" of resources into their own sets,
	//   some combinatorial effects are inevitable, making persistent descriptor sets less practical.
	// - Hybrid solutions are possible, but complexity is increased for little obvious gain.
	//
	// 2. Bindless
	// Bindless descriptor sets have many advantages, and is the direction many people are moving. There's only two cons I can think of:
	// - Not going to be compatible with lower-spec devices any time soon. If you only care about high-end desktop and are willing to rely on an EXT extension, this is not a problem.
	// - Shaders must be written in very specific ways. All shaders must be written in a way where you pass down indices explicitly to the shaders, typically done with push constants.
	//   Then you can load resources by indexing into an array of resources. Pipeline layouts must remain fixed across all pipelines for optimal effect as well.
	// The compatibility concern is the main reason I cannot commit to bindless. You cannot just bolt on bindless after the fact, it is something you need to commit to I think.
	// I also don't have any use cases where bindless solves any problem for me in particular.

	// Here we resolve all "dirty" state before calling vkCmdDispatch, make sure descriptor sets get allocated/found/created and bound to the command buffer.
	// VkPipelines might also be created here, again, in a lazy way.
	cmd->dispatch(1, 1, 1);

	// Could do readbacks here, but that requires some synchronization, which is for another time.
	device.submit(cmd);
}
