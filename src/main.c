#include "main.h"
#include <math.h>
#include <vulkan/vulkan_core.h>

void createDrawImage(Application* app, VmaAllocator allocator)
{
	VkExtent3D extent = {
	    app->width,
	    app->height,
	    1};

	app->drawImage.imageExtent = extent;
	app->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	VkImageUsageFlags usage = 0;
	usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo imgInfo = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = app->drawImage.imageFormat,
	    .extent = extent,
	    .mipLevels = 1,
	    .arrayLayers = 1,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = usage,
	};

	VmaAllocationCreateInfo allocInfo = {
	    .usage = VMA_MEMORY_USAGE_GPU_ONLY,
	    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};

	vmaCreateImage(allocator, &imgInfo, &allocInfo,
	    &app->drawImage.image,
	    &app->drawImage.allocation,
	    NULL);

	app->drawImage.imageView = createImageView(app->device, app->drawImage.image, app->drawImage.imageFormat, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1);
	app->drawExtent.width = extent.width;
	app->drawExtent.height = extent.height;
}

void CopyImagetoImage(
    VkCommandBuffer cmd,
    VkImage source,
    VkImageLayout sourceLayout,
    VkImage destination,
    VkImageLayout destinationLayout,
    VkExtent3D srcExtent,
    VkExtent3D dstExtent,
    VkImageAspectFlags aspectMask,
    uint32_t srcMipLevel,
    uint32_t dstMipLevel,
    uint32_t srcBaseLayer,
    uint32_t dstBaseLayer,
    uint32_t layerCount,
    VkFilter filter)
{
	VkImageBlit2 blitRegion = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
	    .pNext = NULL,
	    .srcSubresource = {
	        .aspectMask = aspectMask,
	        .mipLevel = srcMipLevel,
	        .baseArrayLayer = srcBaseLayer,
	        .layerCount = layerCount,
	    },
	    .dstSubresource = {
	        .aspectMask = aspectMask,
	        .mipLevel = dstMipLevel,
	        .baseArrayLayer = dstBaseLayer,
	        .layerCount = layerCount,
	    },
	};

	// src offsets
	blitRegion.srcOffsets[0] = (VkOffset3D){0, 0, 0};
	blitRegion.srcOffsets[1] = (VkOffset3D){(int32_t)srcExtent.width, (int32_t)srcExtent.height, (int32_t)srcExtent.depth};

	// dst offsets
	blitRegion.dstOffsets[0] = (VkOffset3D){0, 0, 0};
	blitRegion.dstOffsets[1] = (VkOffset3D){(int32_t)dstExtent.width, (int32_t)dstExtent.height, (int32_t)dstExtent.depth};

	VkBlitImageInfo2 blitInfo = {
	    .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
	    .pNext = NULL,
	    .srcImage = source,
	    .srcImageLayout = sourceLayout,
	    .dstImage = destination,
	    .dstImageLayout = destinationLayout,
	    .regionCount = 1,
	    .pRegions = &blitRegion,
	    .filter = filter,
	};

	vkCmdBlitImage2(cmd, &blitInfo);
}

void createSwapchainImageViews(Application* app, VkSwapchainKHR swapchain)
{
	vkGetSwapchainImagesKHR(app->device, swapchain, &app->swapchainImageCount, NULL);
	printf("[Swapchain] Image count: %u\n", app->swapchainImageCount);

	app->swapchainImages = malloc(app->swapchainImageCount * sizeof(VkImage));
	app->swapchainImageViews = malloc(app->swapchainImageCount * sizeof(VkImageView));
	app->presentSemaphores = malloc(app->swapchainImageCount * sizeof(VkSemaphore));

	vkGetSwapchainImagesKHR(app->device, swapchain, &app->swapchainImageCount, app->swapchainImages);

	for (u32 i = 0; i < app->swapchainImageCount; ++i)
	{
		app->swapchainImageViews[i] = createImageView(app->device, app->swapchainImages[i], app->swapchainFormat, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1);
		// Create a per-image semaphore to be signaled when rendering that image completes
		app->presentSemaphores[i] = CreateSemaphore(app->device);
	}

	printf("[Swapchain] ✅ Created %u image views!\n", app->swapchainImageCount);
}

void initCommands(FrameData* frameData, Application* app)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		frameData->commandPools[i] = createCommandBufferPool(app->device, app->physicaldevice);
		frameData->commandBuffers[i] = createCommandBuffer(app->device, frameData->commandPools[i], VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	}
}

void createSyncObjects(FrameData* frameData, Application* app)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		frameData->swapchainSemaphore[i] = CreateSemaphore(app->device);
		frameData->renderSemaphore[i] = CreateSemaphore(app->device);
		frameData->inFlightFences[i] = CreateFence(app->device);
	}
}

int main(void)
{
	Application app = {0};
	app.width = 800;
	app.height = 600;

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);

	volkInitialize();

	app.instance = createInstance();
	volkLoadInstance(app.instance);

	setupDebugMessenger(&app);

	create_surface(&app, window);

	app.physicaldevice = pickPhysicalDevice(app.instance);
	print_gpu_info(app.physicaldevice);

	app.device = createLogicalDevice(app.physicaldevice);
	volkLoadDevice(app.device);

	selectSwapchainFormat(&app);

	VkSwapchainKHR swapchain = createSwapchain(&app);
	createSwapchainImageViews(&app, swapchain);

	// Create VMA allocator and the offscreen draw image
	printf("[VMA] Creating allocator...\n");
	VmaVulkanFunctions vmaFuncs = {0};
	vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	VmaAllocatorCreateInfo allocatorInfo = {0};
	allocatorInfo.instance = app.instance;
	allocatorInfo.physicalDevice = app.physicaldevice;
	allocatorInfo.device = app.device;
	allocatorInfo.pVulkanFunctions = &vmaFuncs;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &app.allocator));
	printf("[VMA] Allocator created. Creating draw image...\n");
	createDrawImage(&app, app.allocator);
	printf("[VMA] Draw image created.\n");

	FrameData frameData = {0};
	initCommands(&frameData, &app);
	createSyncObjects(&frameData, &app);
	app.frameNumber = 0;

	u32 graphicsQueueFamilyIndex = find_graphics_queue_family_index(app.physicaldevice);
	VkQueue graphicsQueue;
	vkGetDeviceQueue(app.device, graphicsQueueFamilyIndex, 0, &graphicsQueue);

	while (!glfwWindowShouldClose(window))
	{

		glfwPollEvents();
		u32 frameIndex = app.frameNumber % MAX_FRAMES_IN_FLIGHT;
		VK_CHECK(vkWaitForFences(app.device, 1, &frameData.inFlightFences[frameIndex], VK_TRUE, UINT64_MAX));
		vkResetFences(app.device, 1, &frameData.inFlightFences[frameIndex]);

		u32 swapchainImageIndex;
		VK_CHECK(vkAcquireNextImageKHR(app.device, swapchain, UINT64_MAX, frameData.swapchainSemaphore[frameIndex], VK_NULL_HANDLE, &swapchainImageIndex));
		VkCommandBuffer cmd = frameData.commandBuffers[frameIndex];
		VK_CHECK(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo cmdinfo = {
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
		vkBeginCommandBuffer(cmd, &cmdinfo);
		// Clear into offscreen draw image
		float flash = fabsf(sinf((float)app.frameNumber / 120.f));
		VkClearColorValue clearValue = {.float32 = {0.0f, 0.0f, flash, 1.0f}};
		VkImageSubresourceRange clearRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1};

		// draw image: UNDEFINED -> GENERAL
		VkImageMemoryBarrier2 drawToGeneral = imageBarrier(
			app.drawImage.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_CLEAR_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1);
		pipelineBarrier(cmd, 0, 0, NULL, 1, &drawToGeneral);

		vkCmdClearColorImage(cmd, app.drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
		app.frameNumber++;

		// Prepare for copy: draw GENERAL -> TRANSFER_SRC, swap UNDEFINED -> TRANSFER_DST
		VkImageMemoryBarrier2 drawToSrc = imageBarrier(
			app.drawImage.image,
			VK_PIPELINE_STAGE_2_CLEAR_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1);

		VkImageMemoryBarrier2 swapToDst = imageBarrier(
			app.swapchainImages[swapchainImageIndex],
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1);
		VkImageMemoryBarrier2 barriersPrep[2] = {drawToSrc, swapToDst};
		pipelineBarrier(cmd, 0, 0, NULL, 2, barriersPrep);

		// execute copy (blit, allows different sizes)
		VkExtent3D srcExtent = {app.drawExtent.width, app.drawExtent.height, 1};
		VkExtent3D dstExtent = {app.width, app.height, 1};
		CopyImagetoImage(cmd,
			app.drawImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			app.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			srcExtent, dstExtent,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 0, 0, 0, 1,
			VK_FILTER_LINEAR);

		// Transition swapchain to PRESENT
		VkImageMemoryBarrier2 toPresent = imageBarrier(
			app.swapchainImages[swapchainImageIndex],
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			0,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1);
		pipelineBarrier(cmd, 0, 0, NULL, 1, &toPresent);

		// finalize the command buffer (we can no longer add commands, but it can now be executed)
		VK_CHECK(vkEndCommandBuffer(cmd));

		// Submit and present
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSemaphore signalForThisImage = app.presentSemaphores[swapchainImageIndex];

		VkSemaphoreSubmitInfo waitSemaphoreInfo = {
		    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		    .semaphore = frameData.swapchainSemaphore[frameIndex],
		    .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT};

		VkSemaphoreSubmitInfo signalSemaphoreInfo = {
		    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		    .semaphore = signalForThisImage,
		    .stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};

		VkCommandBufferSubmitInfo cmdBufferInfo = {
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		    .commandBuffer = cmd};

		VkSubmitInfo2 submit = {
		    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .waitSemaphoreInfoCount = 1,
		    .pWaitSemaphoreInfos = &waitSemaphoreInfo,
		    .commandBufferInfoCount = 1,
		    .pCommandBufferInfos = &cmdBufferInfo,
		    .signalSemaphoreInfoCount = 1,
		    .pSignalSemaphoreInfos = &signalSemaphoreInfo};
		VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, frameData.inFlightFences[frameIndex]));

		VkPresentInfoKHR present = {
		    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		    .waitSemaphoreCount = 1,
		    .pWaitSemaphores = &signalForThisImage,
		    .swapchainCount = 1,
		    .pSwapchains = &swapchain,
		    .pImageIndices = &swapchainImageIndex};
		VK_CHECK(vkQueuePresentKHR(graphicsQueue, &present));
	}

	// Ensure GPU work is complete before destroying resources
	vkDeviceWaitIdle(app.device);

	// destroy draw image resources
	if (app.drawImage.imageView) vkDestroyImageView(app.device, app.drawImage.imageView, NULL);
	if (app.drawImage.image) vmaDestroyImage(app.allocator, app.drawImage.image, app.drawImage.allocation);

	for (u32 i = 0; i < app.swapchainImageCount; ++i)
	{
		vkDestroyImageView(app.device, app.swapchainImageViews[i], NULL);
		vkDestroySemaphore(app.device, app.presentSemaphores[i], NULL);
	}

	free(app.swapchainImageViews);
	free(app.swapchainImages);
	free(app.presentSemaphores);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(app.device, frameData.swapchainSemaphore[i], NULL);
		vkDestroySemaphore(app.device, frameData.renderSemaphore[i], NULL);
		vkDestroyFence(app.device, frameData.inFlightFences[i], NULL);
		vkDestroyCommandPool(app.device, frameData.commandPools[i], NULL);
	}
	vkDestroySwapchainKHR(app.device, swapchain, NULL);
	if (app.allocator) vmaDestroyAllocator(app.allocator);
	vkDestroyDevice(app.device, NULL);
	vkDestroySurfaceKHR(app.instance, app.surface, NULL);
	cleanupDebugMessenger(&app);
	vkDestroyInstance(app.instance, NULL);

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
