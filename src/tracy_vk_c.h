// tracy_vk_c.h  — C99 wrapper around TracyVulkan.hpp (public C API)
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <vulkan/vulkan.h>

// Opaque handles
typedef struct TracyVkCtxC TracyVkCtxC;
typedef struct TracyVkZoneC TracyVkZoneC;

// --- Context lifecycle ---
TracyVkCtxC* tracy_vk_context_create(
    VkPhysicalDevice physdev,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdbuf /* a throwaway setup cmdbuf for init */
);

TracyVkCtxC* tracy_vk_context_create_calibrated(
    VkPhysicalDevice physdev,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdbuf,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct
);

// Optional: host-calibrated path (requires VK_EXT_host_query_reset)
TracyVkCtxC* tracy_vk_context_create_host_calibrated(
    VkPhysicalDevice physdev,
    VkDevice device,
    PFN_vkResetQueryPoolEXT qpreset,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct
);

void tracy_vk_context_destroy(TracyVkCtxC* ctx);

// --- Optional: give the context a name in Tracy UI ---
void tracy_vk_context_name(TracyVkCtxC* ctx, const char* name);

// --- Per-frame collection (flush timestamp queries to Tracy) ---
void tracy_vk_collect(TracyVkCtxC* ctx, VkCommandBuffer cmdbuf);
void tracy_vk_collect_host(TracyVkCtxC* ctx); // if you don’t want to use a cmdbuf

// --- Zones (GPU scope markers) ---
// Begin a GPU zone; returns an opaque zone you must end with tracy_vk_zone_end().
// Pass your current recording command buffer here.
TracyVkZoneC* tracy_vk_zone_begin(
    TracyVkCtxC* ctx,
    VkCommandBuffer cmdbuf,
    const char* name,         // custom label (shown in Tracy)
    const char* file,         // usually __FILE__
    const char* function,     // usually __func__
    uint32_t line,            // usually __LINE__
    int active                // 0 or nonzero; lets you compile out by runtime flag
);

// End the zone (must be paired with a successful begin).
void tracy_vk_zone_end(TracyVkZoneC* zone);

#ifdef __cplusplus
}
#endif

