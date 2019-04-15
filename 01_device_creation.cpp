#include "vulkan.hpp"
#include "device.hpp"
#include "util.hpp"

int main()
{
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
}
