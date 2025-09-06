#ifndef MAIN_H
#define MAIN_H

#include "types.h"
#include <GLFW/glfw3.h>
#include "../external/VulkanMemoryAllocator/include/vk_mem_alloc.h"

// Structs

typedef struct AllocatedImage
{
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
} AllocatedImage;

typedef struct AllocatedBuffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
} AllocatedBuffer;

typedef struct Application // Moved to top
{
	VkInstance instance;
	VkPhysicalDevice physicaldevice;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkDevice device;
	VmaAllocator allocator;
	VkSurfaceKHR surface;
	GLFWwindow* window; // glfw window handle for callbacks/size
	u32 width;
	u32 height;
	bool framebufferResized; // set by GLFW callback on resize
	u64 frameNumber;
	VkFormat swapchainFormat;
	VkColorSpaceKHR swapchainColorSpace;
	VkImage* swapchainImages;
	VkImageView* swapchainImageViews;
	u32 swapchainImageCount;
	VkSwapchainKHR swapchain; // keep current swapchain handle
	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;
	// Per-swapchain-image semaphore signaled on render complete and waited by present
	VkSemaphore* presentSemaphores;
	AllocatedImage drawImage; // High-precision offscreen render target
	VkExtent3D drawExtent;    // Resolution of drawImage
	AllocatedBuffer curveVertexBuffer;
	u32 curveVertexCount;
} Application;
#define MAX_FRAMES_IN_FLIGHT 2

typedef struct FrameData
{
	VkCommandPool commandPools[MAX_FRAMES_IN_FLIGHT];
	VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore swapchainSemaphore[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderSemaphore[MAX_FRAMES_IN_FLIGHT];
	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
} FrameData;

// Function Declarations
// from helpers.c
VkBool32 hasStencil(VkFormat f);
VkShaderModule LoadShaderModule(const char* filepath, VkDevice device);
VkFence CreateFence(VkDevice device);
VkImageMemoryBarrier2 imageBarrier(VkImage image, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkImageLayout currentLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout newLayout, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount);
void pipelineBarrier(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, size_t bufferBarrierCount, const VkBufferMemoryBarrier2* bufferBarriers, size_t imageBarrierCount, const VkImageMemoryBarrier2* imageBarriers);

// from main.c
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // Added declaration
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
VkInstance createInstance();
void setupDebugMessenger(Application* app);
void cleanupDebugMessenger(Application* app);
VkPhysicalDevice pickPhysicalDevice(VkInstance instance);
void print_gpu_info(VkPhysicalDevice device);
u32 find_graphics_queue_family_index(VkPhysicalDevice pickedPhysicalDevice);
VkDevice createLogicalDevice(VkPhysicalDevice pickedphysicaldevice);
void create_surface(Application* app, GLFWwindow* window);
void selectSwapchainFormat(Application* app);
VkSwapchainKHR createSwapchain(Application* app);
VkShaderModule LoadShaderModule(const char* filepath, VkDevice device);
VkCommandPool createCommandBufferPool(VkDevice device, VkPhysicalDevice physicaldevice);
VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel cBLevel);
VkPipelineLayout createPipelineLayout(
    VkDevice device,
    const VkDescriptorSetLayout* setLayouts, uint32_t setLayoutCount,
    const VkPushConstantRange* pushRanges, uint32_t pushRangeCount);

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageViewType viewType, u32 baseMipLevel, u32 levelCount, u32 baseArrayLayer, u32 layerCount);
void createSwapchainImageViews(Application* app, VkSwapchainKHR swapchain);
VkSemaphore CreateSemaphore(VkDevice device);

// Resize / swapchain recreation
void recreate_swapchain(Application* app);
void destroy_swapchain_resources(Application* app);
void glfw_framebuffer_resize_callback(GLFWwindow* window, int width, int height);

#endif // MAIN_H
