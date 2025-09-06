
#pragma once
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* TracyVkCtxHandle;

TracyVkCtxHandle tracy_vk_context_create(
    VkPhysicalDevice physdev, VkDevice device,
    VkQueue queue, VkCommandBuffer cmdbuf
);

void tracy_vk_context_destroy(TracyVkCtxHandle ctx);

void tracy_vk_context_name(TracyVkCtxHandle ctx, const char* name);

void tracy_vk_collect(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf);

void tracy_vk_zone(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf, const char* name);

void tracy_vk_zone_c(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf,
                     const char* name, uint32_t color);

#ifdef __cplusplus
}
#endif
