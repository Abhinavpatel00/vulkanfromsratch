#include "main.h"

VkImageMemoryBarrier2 imageBarrier(VkImage image, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkImageLayout currentLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout newLayout, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount)
{
	VkImageMemoryBarrier2 result = {
	    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

	    .srcStageMask = srcStageMask,
	    .srcAccessMask = srcAccessMask,
	    .dstStageMask = dstStageMask,
	    .dstAccessMask = dstAccessMask,
	    .oldLayout = currentLayout,
	    .newLayout = newLayout,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = image,
	    .subresourceRange.aspectMask = aspectMask,
	    .subresourceRange.baseMipLevel = baseMipLevel,
	    .subresourceRange.levelCount = levelCount,
	    .subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	return result;
}

void pipelineBarrier(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, size_t bufferBarrierCount, const VkBufferMemoryBarrier2* bufferBarriers, size_t imageBarrierCount, const VkImageMemoryBarrier2* imageBarriers)
{
	VkDependencyInfo dependencyInfo = {
	    VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	    .dependencyFlags = dependencyFlags,
	    .bufferMemoryBarrierCount = (u32)(bufferBarrierCount),
	    .pBufferMemoryBarriers = bufferBarriers,
	    .imageMemoryBarrierCount = (u32)(imageBarrierCount),
	    .pImageMemoryBarriers = imageBarriers,
	};
	vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
	(void)messageSeverity;
	(void)messageType;
	(void)pUserData;
	printf("Validation layer: %s\n", pCallbackData->pMessage);
	return VK_FALSE;
}

VkBool32 hasStencil(VkFormat f)
{
	return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkShaderModule LoadShaderModule(const char* filepath, VkDevice device)
{
	FILE* file = fopen(filepath, "rb");
	assert(file);

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	assert(length >= 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = (char*)malloc(length);
	assert(buffer);

	size_t rc = fread(buffer, 1, length, file);
	assert(rc == (size_t)length);
	fclose(file);

	VkShaderModuleCreateInfo createInfo = {0};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = length;
	createInfo.pCode = (const u32*)buffer;

	VkShaderModule shaderModule;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, NULL, &shaderModule));

	free(buffer);
	return shaderModule;
}

// we might need mutiple command pools per thread for multi threading or you need to have 1 VkCommandPool and 1 VkCommandBuffer per thread
// commandpool manages command buffers so its command buffer pool
VkCommandPool createCommandBufferPool(VkDevice device, VkPhysicalDevice physicaldevice)
{
	u32 queueFamilyIndex = find_graphics_queue_family_index(physicaldevice);

	VkCommandPoolCreateInfo poolInfo = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	    .queueFamilyIndex = queueFamilyIndex,
	};
	VkCommandPool commandPool;
	VK_CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &commandPool));
	return commandPool;
}

VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel cBLevel)
{
	VkCommandBufferAllocateInfo allocInfo = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = commandPool,
	    .level = cBLevel,
	    .commandBufferCount = 1,
	};
	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
	return commandBuffer;
}

VkPipelineLayout createPipelineLayout(
    VkDevice device,
    const VkDescriptorSetLayout* setLayouts, uint32_t setLayoutCount,
    const VkPushConstantRange* pushRanges, uint32_t pushRangeCount)
{
	VkPipelineLayoutCreateInfo ci = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = setLayoutCount,
	    .pSetLayouts = setLayouts,                // NULL ok if count==0
	    .pushConstantRangeCount = pushRangeCount, // 0 if no pushes
	    .pPushConstantRanges = pushRanges,        // NULL ok if count==0
	};

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &ci, NULL, &layout));
	return layout;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageViewType viewType, u32 baseMipLevel, u32 levelCount, u32 baseArrayLayer, u32 layerCount)

{
	VkImageAspectFlags aspect =
	    (format == VK_FORMAT_D16_UNORM ||
	        format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
	        format == VK_FORMAT_D32_SFLOAT ||
	        format == VK_FORMAT_D24_UNORM_S8_UINT ||
	        format == VK_FORMAT_D32_SFLOAT_S8_UINT)
	        ? VK_IMAGE_ASPECT_DEPTH_BIT
	        : VK_IMAGE_ASPECT_COLOR_BIT;

	if (hasStencil(format))
	{
		aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	VkImageViewCreateInfo ci = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .image = image,
	    .viewType = viewType,
	    .format = format,
	    .components = {
	        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
	        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
	        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
	        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
	    },
	    .subresourceRange = {
	        .aspectMask = aspect,
	        .baseMipLevel = baseMipLevel,
	        .levelCount = levelCount,
	        .baseArrayLayer = baseArrayLayer,
	        .layerCount = layerCount,
	    },
	};

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &ci, NULL, &view));
	return view;
}

VkSemaphore CreateSemaphore(VkDevice device)
{

	VkSemaphoreCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

	VkSemaphore semaphore;
	vkCreateSemaphore(device, &ci, 0, &semaphore);
	return semaphore;
}

VkFence CreateFence(VkDevice device)

{
	VkFenceCreateInfo ci = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = VK_FENCE_CREATE_SIGNALED_BIT, // create in signalled state so we dont have to wait on first frame
	};

	VkFence fence;
	vkCreateFence(device, &ci, 0, &fence);
	return fence;
}
