#include "main.h"
#include "../external/tracy/public/tracy/TracyC.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include <string.h>
#include "tracy_vk_c.h"
void immediate_submit_copy(Application* app, FrameData* frameData, AllocatedBuffer src, AllocatedBuffer dst, size_t size)
{
	u32 graphicsQueueFamilyIndex = find_graphics_queue_family_index(app->physicaldevice);
	VkQueue graphicsQueue;
	vkGetDeviceQueue(app->device, graphicsQueueFamilyIndex, 0, &graphicsQueue);

	VkCommandBuffer cmd = createCommandBuffer(app->device, frameData->commandPools[0], VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	VkCommandBufferBeginInfo cmdBeginInfo = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmdBeginInfo);

	VkBufferCopy copy = {
	    .srcOffset = 0,
	    .dstOffset = 0,
	    .size = size,
	};
	vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &copy);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd,
	};
	vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);

	vkFreeCommandBuffers(app->device, frameData->commandPools[0], 1, &cmd);
}

AllocatedBuffer create_buffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = allocSize,
	    .usage = usage,
	};

	VmaAllocationCreateInfo vmaallocInfo = {
	    .usage = memoryUsage,
	};

	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, NULL));

	return newBuffer;
}

void create_curve_data(Application* app, FrameData* frameData, VmaAllocator allocator)
{
	const int num_segments = 1000;
	app->curveVertexCount = num_segments + 1;
	size_t bufferSize = app->curveVertexCount * 3 * sizeof(float);

	float* vertices = malloc(bufferSize);

	float a = 0.4f;
	float b = 0.2f;

	for (int i = 0; i <= num_segments; ++i)
	{
		float t = (float)i / (float)num_segments * 20.0f;
		vertices[i * 3 + 0] = a * cos(t);
		vertices[i * 3 + 1] = a * sin(t);
		vertices[i * 3 + 2] = b * (t - 10.0f);
	}

	AllocatedBuffer stagingBuffer = create_buffer(allocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, vertices, bufferSize);
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	free(vertices);

	app->curveVertexBuffer = create_buffer(allocator, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	immediate_submit_copy(app, frameData, stagingBuffer, app->curveVertexBuffer, bufferSize);

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

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

void CopyImagetoImage(VkCommandBuffer cmd, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout,
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
	    .srcImage = src,
	    .srcImageLayout = srcLayout,
	    .dstImage = dst,
	    .dstImageLayout = dstLayout,
	    .regionCount = 1,
	    .pRegions = &blitRegion,
	    .filter = filter,
	};

	vkCmdBlitImage2(cmd, &blitInfo);
}

static void update_storage_image_descriptor(Application* app, VkDescriptorSet descriptorSet)
{
	VkDescriptorImageInfo storageInfo = {
	    .sampler = VK_NULL_HANDLE,
	    .imageView = app->drawImage.imageView,
	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};
	VkWriteDescriptorSet write = {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 0,
	    .dstArrayElement = 0,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	    .pImageInfo = &storageInfo,
	};
	vkUpdateDescriptorSets(app->device, 1, &write, 0, NULL);
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

void destroy_swapchain_resources(Application* app)
{
	// Destroy per-image resources
	if (app->swapchainImageViews)
	{
		for (u32 i = 0; i < app->swapchainImageCount; ++i)
		{
			if (app->swapchainImageViews[i])
				vkDestroyImageView(app->device, app->swapchainImageViews[i], NULL);
			if (app->presentSemaphores && app->presentSemaphores[i])
				vkDestroySemaphore(app->device, app->presentSemaphores[i], NULL);
		}
		free(app->swapchainImageViews);
		app->swapchainImageViews = NULL;
	}
	if (app->swapchainImages)
	{
		free(app->swapchainImages);
		app->swapchainImages = NULL;
	}
	if (app->presentSemaphores)
	{
		free(app->presentSemaphores);
		app->presentSemaphores = NULL;
	}
	if (app->swapchain)
	{
		vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
		app->swapchain = VK_NULL_HANDLE;
	}
}

void recreate_swapchain(Application* app)
{
	// Wait until non-zero framebuffer size (minimized windows report 0)
	int width = 0, height = 0;
	do
	{
		glfwGetFramebufferSize(app->window, &width, &height);
		glfwWaitEventsTimeout(0.01);
	} while (width == 0 || height == 0);

	vkDeviceWaitIdle(app->device);

	// Destroy old swapchain-related resources
	destroy_swapchain_resources(app);

	// Update app dims
	app->width = (u32)width;
	app->height = (u32)height;

	// Recreate swapchain and its image views
	selectSwapchainFormat(app);
	app->swapchain = createSwapchain(app);
	createSwapchainImageViews(app, app->swapchain);

	// Recreate draw image to match new size
	if (app->drawImage.imageView)
		vkDestroyImageView(app->device, app->drawImage.imageView, NULL);
	if (app->drawImage.image)
		vmaDestroyImage(app->allocator, app->drawImage.image, app->drawImage.allocation);
	createDrawImage(app, app->allocator);
}

void glfw_framebuffer_resize_callback(GLFWwindow* window, int width, int height)
{
	(void)width;
	(void)height;
	Application* app = (Application*)glfwGetWindowUserPointer(window);
	if (app)
		app->framebufferResized = true;
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
	app.window = window;

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

	app.swapchain = createSwapchain(&app);
	createSwapchainImageViews(&app, app.swapchain);

	// Create VMA allocator and the offscreen draw image
	printf("[VMA] Creating allocator...\n");
	VmaVulkanFunctions vmaFuncs = {0};
	vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	VmaAllocatorCreateInfo allocatorInfo = {
	    .instance = app.instance,
	    .physicalDevice = app.physicaldevice,
	    .device = app.device,
	    .pVulkanFunctions = &vmaFuncs,
	    .vulkanApiVersion = VK_API_VERSION_1_3,
	};
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &app.allocator));
	printf("[VMA] Allocator created. Creating draw image...\n");
	createDrawImage(&app, app.allocator);
	printf("[VMA] Draw image created.\n");

	FrameData frameData = {0};
	initCommands(&frameData, &app);
	

u32 graphicsQueueFamilyIndex = find_graphics_queue_family_index(app.physicaldevice);
VkQueue graphicsQueue;
vkGetDeviceQueue(app.device, graphicsQueueFamilyIndex, 0, &graphicsQueue);

// Pick one command buffer (e.g., first from frameData) to give Tracy
VkCommandBuffer tracyCmd = frameData.commandBuffers[0];

// Create Tracy Vulkan context
TracyVkCtxHandle tracyCtx = tracy_vk_context_create(
    app.physicaldevice,
    app.device,
    graphicsQueue,
    tracyCmd
);
tracy_vk_context_name(tracyCtx, "Main Vulkan Context");


	createSyncObjects(&frameData, &app);
	create_curve_data(&app, &frameData, app.allocator);
	app.frameNumber = 0;
	VkDescriptorPoolSize poolSizes[] = {
	    {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 10},
	    {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 10}};
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 10; // number of descriptor sets we can allocate
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	VkDescriptorPool descriptorPool;
	VK_CHECK(vkCreateDescriptorPool(app.device, &poolInfo, NULL, &descriptorPool));
	// 2. Create descriptor set layout
	VkDescriptorSetLayoutBinding imageBinding = {};
	imageBinding.binding = 0;
	imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	imageBinding.descriptorCount = 1; // one image at binding 0
	imageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	imageBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding bufferBinding = {};
	bufferBinding.binding = 1;
	bufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bufferBinding.descriptorCount = 1;
	bufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding bindings[] = {imageBinding, bufferBinding};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;
	VkDescriptorSetLayout drawImageDescriptorLayout;
	VK_CHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, NULL, &drawImageDescriptorLayout));

	// 3. Allocate descriptor set(s)
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &drawImageDescriptorLayout;

	VkDescriptorSet descriptorSet;
	VK_CHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));

	// 4. Update descriptor with draw image as storage image
	update_storage_image_descriptor(&app, descriptorSet);

	VkDescriptorBufferInfo bufferInfo = {
	    .buffer = app.curveVertexBuffer.buffer,
	    .offset = 0,
	    .range = app.curveVertexCount * 3 * sizeof(float),
	};

	VkWriteDescriptorSet bufferWrite = {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 1,
	    .dstArrayElement = 0,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo = &bufferInfo,
	};
	vkUpdateDescriptorSets(app.device, 1, &bufferWrite, 0, NULL);

	// 5. Create compute pipeline for compiledshaders/grad.comp.spv
	VkPipelineLayout computePipelineLayout;
	{
		VkPushConstantRange pushConstantRange = {
		    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		    .offset = 0,
		    .size = sizeof(u32),
		};

		VkPipelineLayoutCreateInfo plInfo = {
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		    .setLayoutCount = 1,
		    .pSetLayouts = &drawImageDescriptorLayout,
		    .pushConstantRangeCount = 1,
		    .pPushConstantRanges = &pushConstantRange,
		};
		VK_CHECK(vkCreatePipelineLayout(app.device, &plInfo, NULL, &computePipelineLayout));
	}

	VkPipeline computePipeline;
	{
		VkShaderModule compModule = LoadShaderModule("compiledshaders/grad.comp.spv", app.device);
		VkPipelineShaderStageCreateInfo stage = {
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
		    .module = compModule,
		    .pName = "main",
		};
		VkComputePipelineCreateInfo cpInfo = {
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .stage = stage,
		    .layout = computePipelineLayout,
		};
		VK_CHECK(vkCreateComputePipelines(app.device, VK_NULL_HANDLE, 1, &cpInfo, NULL, &computePipeline));
		vkDestroyShaderModule(app.device, compModule, NULL);
	}
	// Hook resize callback and user pointer
	glfwSetWindowUserPointer(window, &app);
	glfwSetFramebufferSizeCallback(window, glfw_framebuffer_resize_callback);

	while (!glfwWindowShouldClose(window))
	{

		glfwPollEvents();
		if (app.framebufferResized)
		{
			recreate_swapchain(&app);
			update_storage_image_descriptor(&app, descriptorSet);
			app.framebufferResized = false;
		}
		u32 frameIndex = app.frameNumber % MAX_FRAMES_IN_FLIGHT;
		VK_CHECK(vkWaitForFences(app.device, 1, &frameData.inFlightFences[frameIndex], VK_TRUE, UINT64_MAX));
		vkResetFences(app.device, 1, &frameData.inFlightFences[frameIndex]);

		u32 swapchainImageIndex;
		VkResult acq = vkAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX, frameData.swapchainSemaphore[frameIndex], VK_NULL_HANDLE, &swapchainImageIndex);
		if (acq == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreate_swapchain(&app);
			update_storage_image_descriptor(&app, descriptorSet);
			continue;
		}
		if (acq == VK_SUBOPTIMAL_KHR)
		{
			// Suboptimal is okay; proceed this frame and schedule a recreate
			app.framebufferResized = true;
		}
		else
		{
			VK_CHECK(acq);
		}
		VkCommandBuffer cmd = frameData.commandBuffers[frameIndex];
		VK_CHECK(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo cmdinfo = {
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
		vkBeginCommandBuffer(cmd, &cmdinfo);
		tracy_vk_collect(tracyCtx, cmd);

		// Prepare draw image for compute writes: UNDEFINED -> GENERAL
		VkImageMemoryBarrier2 drawToGeneral = imageBarrier(
		    app.drawImage.image,
		    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		    0,
		    VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		    VK_ACCESS_2_SHADER_WRITE_BIT,
		    VK_IMAGE_LAYOUT_GENERAL,
		    VK_IMAGE_ASPECT_COLOR_BIT,
		    0, 1);
		pipelineBarrier(cmd, 0, 0, NULL, 1, &drawToGeneral);

		tracy_vk_zone(tracyCtx, cmd, "Compute Dispatch");
		// Dispatch grad.comp to fill the draw image
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &descriptorSet, 0, NULL);
		vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u32), &app.curveVertexCount);
		uint32_t gx = (app.drawExtent.width + 15u) / 16u;
		uint32_t gy = (app.drawExtent.height + 15u) / 16u;
		vkCmdDispatch(cmd, gx, gy, 1);

		// Prepare for copy: draw GENERAL -> TRANSFER_SRC, swap UNDEFINED -> TRANSFER_DST
		VkImageMemoryBarrier2 drawToSrc = imageBarrier(
		    app.drawImage.image,
		    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		    VK_ACCESS_2_SHADER_WRITE_BIT,
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
		    .pSwapchains = &app.swapchain,
		    .pImageIndices = &swapchainImageIndex};
		VkResult pres = vkQueuePresentKHR(graphicsQueue, &present);
		if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR)
		{
			recreate_swapchain(&app);
			update_storage_image_descriptor(&app, descriptorSet);
		}
		else
		{
			VK_CHECK(pres);
		}
		app.frameNumber++;

		TracyCFrameMark;
	}

	// Ensure GPU work is complete before destroying resources
	vkDeviceWaitIdle(app.device);
tracy_vk_context_destroy(tracyCtx);

	// destroy draw image resources
	if (app.drawImage.imageView)
		vkDestroyImageView(app.device, app.drawImage.imageView, NULL);
	if (app.drawImage.image)
		vmaDestroyImage(app.allocator, app.drawImage.image, app.drawImage.allocation);

	vmaDestroyBuffer(app.allocator, app.curveVertexBuffer.buffer, app.curveVertexBuffer.allocation);

	destroy_swapchain_resources(&app);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(app.device, frameData.swapchainSemaphore[i], NULL);
		vkDestroySemaphore(app.device, frameData.renderSemaphore[i], NULL);
		vkDestroyFence(app.device, frameData.inFlightFences[i], NULL);
		vkDestroyCommandPool(app.device, frameData.commandPools[i], NULL);
	}
	// swapchain already destroyed by destroy_swapchain_resources
	// Destroy compute/descriptor objects
	vkDestroyPipeline(app.device, computePipeline, NULL);
	vkDestroyPipelineLayout(app.device, computePipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(app.device, drawImageDescriptorLayout, NULL);
	vkDestroyDescriptorPool(app.device, descriptorPool, NULL);
	if (app.allocator)
		vmaDestroyAllocator(app.allocator);
	vkDestroyDevice(app.device, NULL);
	vkDestroySurfaceKHR(app.instance, app.surface, NULL);
	cleanupDebugMessenger(&app);
	vkDestroyInstance(app.instance, NULL);

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
