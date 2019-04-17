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

int main()
{
	// This is here to load libvulkan.so/libvulkan-1.dll/etc.
	// We do this once since we can have multiple devices around.
	// It is possible to pass in a custom pointer to vkGetInstanceProcAddr.
	// This is useful if the user loads the Vulkan loader in a custom way
	// and we can bootstrap ourselves straight from vkGetInstanceProcAddr rather
	// than loading Vulkan dynamically. This is common for GLFW for example.
	if (!Vulkan::Context::init_loader(nullptr))
	{
		LOGE("Failed to create loader!\n");
		return 1;
	}

	// NOTE: The Vulkan symbols are function pointers and are provided by the "volk" project.

	// The context is responsible for:
	// - Creating VkInstance
	// - Creating VkDevice
	// - Setting up VkQueues for graphics, compute and transfer.
	// - Setting up validation layers.
	// - Creating debug callbacks.
	Vulkan::Context context;

	// We don't pass in any extensions here. Normally we would pass in at least
	// VK_KHR_surface and the platform surface extension for instance extensions,
	// and VK_KHR_swapchain for device extensions.
	// Vulkan::Context owns the instance and device.
	// There are also interfaces for giving pre-existing instances and/or devices to the Vulkan::Context.
	// This might be useful if a VkInstance is already provided for example.
	if (!context.init_instance_and_device(nullptr, 0, nullptr, 0))
	{
		LOGE("Failed to create VkInstance and VkDevice.\n");
		return 1;
	}

	Vulkan::Device device;
	device.set_context(context);

	// Granite is a C++ project and thus takes advantage of RAII.
	// We get appropriate cleanup here when device and context go out of scope.
}
