// tracy_vk_c.cpp — C++ shim that wraps TracyVulkan.hpp with a C ABI
#define TRACY_ENABLE 1
// #define TRACY_HAS_CALLSTACK 1   // enable if you want callstacks
// #define TRACY_CALLSTACK 10      // example callstack depth
// #define TRACY_ON_DEMAND         // optional: only record when UI is connected

#include <vulkan/vulkan.h>
#include "../external/tracy/public/tracy/TracyVulkan.hpp" 
// from Tracy
#include "../external/tracy/public/tracy/Tracy.hpp"

extern "C" {

struct TracyVkCtxC {
    TracyVkCtx ctx; // this is tracy::VkCtx*, typedef’d to TracyVkCtx
};

struct TracyVkZoneC {
    // We heap-allocate VkCtxScope so we can end it later; dtor logs the end.
    tracy::VkCtxScope* scope;
};

// --- Contexts ---

TracyVkCtxC* tracy_vk_context_create(
    VkPhysicalDevice physdev,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdbuf
) {
    TracyVkCtx c = TracyVkContext( physdev, device, queue, cmdbuf );
    if (!c) return nullptr;
    auto* out = new TracyVkCtxC{ c };
    return out;
}

TracyVkCtxC* tracy_vk_context_create_calibrated(
    VkPhysicalDevice physdev,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdbuf,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct
) {
    TracyVkCtx c = TracyVkContextCalibrated( physdev, device, queue, cmdbuf, gpdctd, gct );
    if (!c) return nullptr;
    auto* out = new TracyVkCtxC{ c };
    return out;
}

TracyVkCtxC* tracy_vk_context_create_host_calibrated(
    VkPhysicalDevice physdev,
    VkDevice device,
    PFN_vkResetQueryPoolEXT qpreset,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct
) {
#if defined(VK_EXT_host_query_reset)
    TracyVkCtx c = TracyVkContextHostCalibrated( physdev, device, qpreset, gpdctd, gct );
    if (!c) return nullptr;
    auto* out = new TracyVkCtxC{ c };
    return out;
#else
    (void)physdev; (void)device; (void)qpreset; (void)gpdctd; (void)gct;
    return nullptr;
#endif
}

void tracy_vk_context_destroy(TracyVkCtxC* ctx) {
    if (!ctx) return;
    TracyVkDestroy( ctx->ctx );
    delete ctx;
}

// --- Naming ---

void tracy_vk_context_name(TracyVkCtxC* ctx, const char* name) {
    if (!ctx || !name) return;
    TracyVkContextName( ctx->ctx, name, (uint16_t) strlen(name) );
}

// --- Collection ---

void tracy_vk_collect(TracyVkCtxC* ctx, VkCommandBuffer cmdbuf) {
    if (!ctx) return;
    TracyVkCollect( ctx->ctx, cmdbuf );
}

void tracy_vk_collect_host(TracyVkCtxC* ctx) {
    if (!ctx) return;
    TracyVkCollectHost( ctx->ctx );
}

// --- Zones ---

TracyVkZoneC* tracy_vk_zone_begin(
    TracyVkCtxC* cctx,
    VkCommandBuffer cmdbuf,
    const char* name,
    const char* file,
    const char* function,
    uint32_t line,
    int active
) {
    if (!cctx) return nullptr;
    // Use constructor that builds a source location on the fly.
    auto* z = new TracyVkZoneC;
    z->scope = new tracy::VkCtxScope(
        cctx->ctx,
        line,
        file, file ? strlen(file) : 0,
        function, function ? strlen(function) : 0,
        name ? name : "", name ? strlen(name) : 0,
        cmdbuf,
        active != 0 // bool
    );
    return z;
}

void tracy_vk_zone_end(TracyVkZoneC* zone) {
    if (!zone) return;
    delete zone->scope;  // ~VkCtxScope() records the end + second timestamp
    delete zone;
}

} // extern "C"

