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

// For simplicity sake, we have used glslc (shaderc project) to compile GLSL to SPIR-V, expressed as a
// nice little uint32_t C array.
// Granite has shader manager which is far more sophisticated than this, but it is built on top of the basic shader
// creation API in Granite, and is not really important from a Vulkan API design point of view.
static const uint32_t simple_vert[] =
#include "shaders/simple.vert.inc"
;

static const uint32_t simple_frag[] =
#include "shaders/simple.frag.inc"
;

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

	// Shaders and programs are considered to be persistent objects in Granite. Once a Shader handle has been requested
	// it is permanent. Vulkan::Device owns the handle, and therefore Vulkan::Shader and Vulkan::Program are simply pointers to some internally allocated data structure.
	// This is where we start to see a common theme in Granite, where we hash input data and translate that to a persistent data structure.

	// In Vulkan, a pipeline will require all shader stages to be combined into one pipeline.
	// Descriptor set layouts and pipeline layouts also require shader stages to be together.

	// When a new shader is found, a Vulkan::Shader object is created.
	// At this point, we also perform reflection with SPIRV-Cross.
	// For each shader, we need to know which binding points and locations are active in the shader,
	// as well as how many bytes of push constants are in use.
	// In theory, if SPIR-V modules are built offline and shipped as-is, we could also provide the reflection info as side-band data
	// without having to bundle a reflection library, but I never felt the need to this plumbing exercise yet.

	// An important thing to note is that we do *not* reflect any names, only semantically important decorations
	// like bindings, locations and descriptor sets.
	// The binding model is fully index based, no GL-style "glGetUniformLocation" shenanigans.
	Vulkan::Shader *vert = device.request_shader(simple_vert, sizeof(simple_vert));
	Vulkan::Shader *frag = device.request_shader(simple_frag, sizeof(simple_frag));

	// There is no real work done here, except that once we know all shader stages, we finally know
	// an appropriate pipeline layout. We essentially take the union of all resources used in the two graphics stages here.
	// At this point, we create look at all active descriptor sets and create new Vulkan::DescriptorSetAllocator objects internally.
	// These internal objects are of course hashed. A Vulkan::DescriptorSetAllocator internally can be represented as:
	// - The VkDescriptorSetLayout.
	// - The resource binding signature for the set.
	// - A recycling allocator which is designed to allocate and recycle VkDescriptorSets of this particular VkDescriptorSetLayout.
	//   The recycling allocator design is kinda cool, and is explored in later samples.

	// Automatically deducing pipeline layouts is one of the biggest convenience features of Granite.
	// There is very little gain from hand-crafting pipeline layouts - and as you will know if you have done it -
	// it is ridiculously tedious (and error prone!) to hand-write these structures.

	// Once we have a list of VkDescriptorSetLayouts and push constant layouts, we now have our Vulkan::PipelineLayout.
	// This is of course, hashed as well based on the hash of descriptor set layouts and push constant ranges.
	Vulkan::Program *program = device.request_program(vert, frag);

	// We are not at the point where we can translate Program to a Pipeline. For that we will need to add lot more
	// state as you will be familiar with if you wrote some Vulkan code before.
	// This is for later samples.
}
