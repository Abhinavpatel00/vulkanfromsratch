#include "main.h"

VkInstance createInstance()
{
	printf("Initializing Vulkan instance...\n");

	VkApplicationInfo appInfo = {
	    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pNext = NULL,
	    .pApplicationName = "Vulkan Test",
	    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
	    .pEngineName = "No Engine",
	    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
	    .apiVersion = VK_API_VERSION_1_3,
	};

	VkInstanceCreateInfo createInfo = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pNext = NULL,
	    .flags = 0,
	    .pApplicationInfo = &appInfo,
	};

#ifdef _DEBUG
	const char* debugLayers[] = {"VK_LAYER_KHRONOS_validation"};
	createInfo.enabledLayerCount = ARRAYSIZE(debugLayers);
	createInfo.ppEnabledLayerNames = debugLayers;

	printf("Enabling debug layers:\n");
	for (u32 i = 0; i < createInfo.enabledLayerCount; ++i)
		printf("  %s\n", debugLayers[i]);

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
	    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	    .pNext = NULL,
	    .flags = 0,
	    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
	    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	    .pfnUserCallback = debugCallback,
	    .pUserData = NULL,
	};

	createInfo.pNext = &debugCreateInfo;
#else
	createInfo.enabledLayerCount = 0;
	createInfo.ppEnabledLayerNames = NULL;
#endif

	u32 glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	assert(glfwExtensionCount < 16);

	const char* extensions[16];
	for (u32 i = 0; i <glfwExtensionCount; ++i)
	{
		extensions[i] = glfwExtensions[i];
		printf("Adding GLFW extension: %s\n", extensions[i]);
	}

	u32 extensionCount = glfwExtensionCount;

#ifdef _DEBUG
	extensions[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	printf("Adding debug extension: %s\n", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	createInfo.enabledExtensionCount = extensionCount;
	createInfo.ppEnabledExtensionNames = extensions;

	VkInstance inst;
	printf("Creating Vulkan instance...\n");
	VK_CHECK(vkCreateInstance(&createInfo, NULL, &inst));
	printf("Vulkan instance created successfully!\n");

	return inst;
}
void setupDebugMessenger(Application* app)
{
#ifdef _DEBUG
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {
	    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	    .pNext = NULL,
	    .flags = 0,
	    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	    .pfnUserCallback = debugCallback,
	    .pUserData = NULL,
	};

	VK_CHECK(vkCreateDebugUtilsMessengerEXT(app->instance, &createInfo, NULL, &app->debugMessenger));

#endif
}

void cleanupDebugMessenger(Application* app)
{
#ifdef _DEBUG
	vkDestroyDebugUtilsMessengerEXT(app->instance, app->debugMessenger, NULL);
#endif
}

VkPhysicalDevice pickPhysicalDevice(VkInstance instance)
{
	u32 deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
	printf("Number of devices: %d\n", deviceCount);

	VkPhysicalDevice devices[8];
	u32 count = ARRAYSIZE(devices);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));

	VkPhysicalDevice selected = VK_NULL_HANDLE;

	for (u32 i = 0; i < count; ++i)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(devices[i], &props);

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
		    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
		{
			printf("GPU%d: %s (%s)\n", i, props.deviceName,
			    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" : "Integrated GPU");
			printf("  Vulkan API: %d.%d.%d\n",
			    VK_VERSION_MAJOR(props.apiVersion),
			    VK_VERSION_MINOR(props.apiVersion),
			    VK_VERSION_PATCH(props.apiVersion));
			printf("  Driver: %d.%d.%d\n",
			    VK_VERSION_MAJOR(props.driverVersion),
			    VK_VERSION_MINOR(props.driverVersion),
			    VK_VERSION_PATCH(props.driverVersion));

			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				selected = devices[i];
			else if (!selected)
				selected = devices[i];
		}
	}

	if (!selected)
	{
		fprintf(stderr, "No suitable GPU found.\n");
		exit(1);
	}

	return selected;
}

void print_gpu_info(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(device, &props);

	printf("\n=== SELECTED GPU ===\n");
	printf("Name: %s\n", props.deviceName);
	printf("Type: %s\n", props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" : "Integrated GPU");
	printf("Vendor ID: 0x%X\n", props.vendorID);
	printf("Device ID: 0x%X\n", props.deviceID);
	printf("Vulkan API: %d.%d.%d\n",
	    VK_VERSION_MAJOR(props.apiVersion),
	    VK_VERSION_MINOR(props.apiVersion),
	    VK_VERSION_PATCH(props.apiVersion));
	printf("Driver: %d.%d.%d\n",
	    VK_VERSION_MAJOR(props.driverVersion),
	    VK_VERSION_MINOR(props.driverVersion),
	    VK_VERSION_PATCH(props.driverVersion));
	printf("Max Texture Size: %d x %d\n",
	    props.limits.maxImageDimension2D,
	    props.limits.maxImageDimension2D);
	printf("Max Uniform Buffer Size: %u MB\n",
	    props.limits.maxUniformBufferRange / (1024 * 1024));
	printf("====================\n\n");

	u32 queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

	VkQueueFamilyProperties* queueFamilies =
	    malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	printf("[Debug] Found %u queue families\n", queueFamilyCount);

	for (u32 i = 0; i < queueFamilyCount; ++i)
	{
		printf("[Debug] QueueFamily[%u]: queueCount=%u, flags=0x%x",
		    i, queueFamilies[i].queueCount, queueFamilies[i].queueFlags);

		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			printf(" (GRAPHICS)");
		if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
			printf(" (COMPUTE)");
		if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
			printf(" (TRANSFER)");
		if (queueFamilies[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)
			printf(" (SPARSE_BINDING)");

		printf("\n");
	}
}
u32 find_graphics_queue_family_index(VkPhysicalDevice pickedPhysicalDevice)
{
	u32 queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(pickedPhysicalDevice,
	    &queueFamilyCount, NULL);
	VkQueueFamilyProperties* queueFamilies =
	    malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(pickedPhysicalDevice,
	    &queueFamilyCount, queueFamilies);
	u32 queuefamilyIndex = UINT32_MAX;
	for (u32 i = 0; i < queueFamilyCount; ++i)
	{
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queuefamilyIndex = i;
			break;
		}
	}
	assert(queuefamilyIndex != UINT32_MAX && "No suitable queue family found");
	free(queueFamilies);
	return queuefamilyIndex;
}
VkDevice createLogicalDevice(VkPhysicalDevice pickedphysicaldevice)
{
    float queuePriorities = 1.0f;

    VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = find_graphics_queue_family_index(pickedphysicaldevice),
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities
    };

    // Enable synchronization2
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext = NULL,
        .synchronization2 = VK_TRUE,
    };

    // Enable dynamic rendering
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &sync2Feature, // chain sync2 AFTER dynamic rendering
        .dynamicRendering = VK_TRUE,
    };

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, // required by dynamic rendering
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,   // required by depth/stencil resolve
        VK_KHR_MULTIVIEW_EXTENSION_NAME,             // required by renderpass2
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME      // required by vkCmdPipelineBarrier2
    };

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamicRenderingFeature, // chain starts here
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = ARRAYSIZE(deviceExtensions),
        .ppEnabledExtensionNames = deviceExtensions,
    };

    VkDevice device;
    VK_CHECK(vkCreateDevice(pickedphysicaldevice, &deviceInfo, NULL, &device));
    return device;
}
void create_surface(Application* app, GLFWwindow* window)
{
	VK_CHECK(glfwCreateWindowSurface(app->instance, window, NULL, &app->surface));
}
void selectSwapchainFormat(Application* app)
{
	u32 formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicaldevice, app->surface, &formatCount, NULL);

	printf("[Swapchain] Available surface formats: %u\n", formatCount);

	if (formatCount == 0)
	{
		fprintf(stderr, "[Swapchain] ❌ No surface formats available!\n");
		return;
	}

	VkSurfaceFormatKHR* formats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
	vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicaldevice, app->surface, &formatCount, formats);

	// Default pick first
	app->swapchainFormat = formats[0].format;
	app->swapchainColorSpace = formats[0].colorSpace;

	for (u32 i = 0; i < formatCount; ++i)
	{
		printf("[Swapchain] Format[%u]: %s, ColorSpace: %s\n",
		    i,
		    vkFormatToString(formats[i].format),
		    vkColorSpaceToString(formats[i].colorSpace));

		if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB)
		{
			printf("[Swapchain] ✅ Chose preferred format: %s + %s\n",
			    vkFormatToString(formats[i].format),
			    vkColorSpaceToString(formats[i].colorSpace));
			app->swapchainFormat = formats[i].format;
			app->swapchainColorSpace = formats[i].colorSpace;
			break;
		}
	}

	printf("[Swapchain] Using format: %s, colorSpace: %s\n",
	    vkFormatToString(app->swapchainFormat),
	    vkColorSpaceToString(app->swapchainColorSpace));

	free(formats);
}

VkSwapchainKHR createSwapchain(Application* app)
{
	u32 queueFamilyIndex = find_graphics_queue_family_index(app->physicaldevice);
	VkBool32 presentSupported = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
	    app->physicaldevice, queueFamilyIndex, app->surface, &presentSupported));
	assert(presentSupported);

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
	    app->physicaldevice, app->surface, &surfaceCapabilities));

	printf("[Swapchain] minImageCount: %u, maxImageCount: %u\n",
	    surfaceCapabilities.minImageCount,
	    surfaceCapabilities.maxImageCount == 0 ? UINT32_MAX : surfaceCapabilities.maxImageCount);
	printf("[Swapchain] Current extent: %u x %u (requested: %u x %u)\n",
	    surfaceCapabilities.currentExtent.width,
	    surfaceCapabilities.currentExtent.height,
	    app->width, app->height);

	VkSwapchainCreateInfoKHR swapchainInfo = {
	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .surface = app->surface,
	    .minImageCount = surfaceCapabilities.minImageCount,
	    .imageFormat = app->swapchainFormat,
	    .imageColorSpace = app->swapchainColorSpace,
	    .imageExtent = {.width = app->width, .height = app->height},
	    .imageArrayLayers = 1,
	    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
	    .clipped = VK_TRUE,
	    .queueFamilyIndexCount = 1,
	    .pQueueFamilyIndices = &queueFamilyIndex,
	};

	printf("[Swapchain] Creating swapchain with:\n");
	printf("   Format: %s\n", vkFormatToString(swapchainInfo.imageFormat));
	printf("   ColorSpace: %s\n", vkColorSpaceToString(swapchainInfo.imageColorSpace));
	printf("   Extent: %u x %u\n", swapchainInfo.imageExtent.width, swapchainInfo.imageExtent.height);
	printf("   PresentMode: MAILBOX_KHR\n");

	VkSwapchainKHR swapchain;
	VK_CHECK(vkCreateSwapchainKHR(app->device, &swapchainInfo, 0, &swapchain));
	printf("[Swapchain] ✅ Swapchain created successfully!\n");

	return swapchain;
}
