#include "main.h"
#include <math.h>
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
		// Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
		VkImageMemoryBarrier2 toTransfer = imageBarrier(
		    app.swapchainImages[swapchainImageIndex],
		    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		    0,
		    VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		    VK_ACCESS_2_TRANSFER_WRITE_BIT,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		    VK_IMAGE_ASPECT_COLOR_BIT,
		    0, 1);
		pipelineBarrier(cmd, 0, 0, NULL, 1, &toTransfer);

		float flash = fabsf(sinf((float)app.frameNumber / 120.f));
		VkClearColorValue clearValue = {.float32 = {0.0f, 0.0f, flash, 1.0f}};

		// clear whole image
		VkImageSubresourceRange clearRange = {
		    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		    .baseMipLevel = 0,
		    .levelCount = 1,
		    .baseArrayLayer = 0,
		    .layerCount = 1};
		vkCmdClearColorImage(
		    cmd,
		    app.swapchainImages[swapchainImageIndex],
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		    &clearValue,
		    1,
		    &clearRange);
		app.frameNumber++;

		// Transition: TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR
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
			.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT
		};
		
		VkSemaphoreSubmitInfo signalSemaphoreInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = signalForThisImage,
			.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
		};
		
		VkCommandBufferSubmitInfo cmdBufferInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cmd
		};
		
		VkSubmitInfo2 submit = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &waitSemaphoreInfo,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cmdBufferInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signalSemaphoreInfo
		};
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
	vkDestroyDevice(app.device, NULL);
	vkDestroySurfaceKHR(app.instance, app.surface, NULL);
	cleanupDebugMessenger(&app);
	vkDestroyInstance(app.instance, NULL);

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
