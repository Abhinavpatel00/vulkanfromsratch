// tracy_vk_c.cpp — C++ shim that wraps TracyVulkan.hpp with a C ABI
#define TRACY_ENABLE 1
// #define TRACY_HAS_CALLSTACK 1   // enable if you want callstacks
// #define TRACY_CALLSTACK 10      // example callstack depth
// #define TRACY_ON_DEMAND         // optional: only record when UI is connected

#include "../external/volk/volk.h"
#include "../external/tracy/public/tracy/TracyVulkan.hpp" 
#include "tracy_vk_c.h"
#include <string.h>

TracyVkCtxHandle tracy_vk_context_create(
    VkPhysicalDevice physdev,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdbuf)
{
    return TracyVkContext(physdev, device, queue, cmdbuf);
}

void tracy_vk_context_destroy(TracyVkCtxHandle ctx)
{
    TracyVkDestroy(static_cast<tracy::VkCtx*>(ctx));
}

void tracy_vk_context_name(TracyVkCtxHandle ctx, const char* name)
{
    TracyVkContextName(static_cast<tracy::VkCtx*>(ctx), name, (uint16_t)strlen(name));
}

void tracy_vk_collect(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf)
{
    static_cast<tracy::VkCtx*>(ctx)->Collect(cmdbuf);
}

void tracy_vk_zone(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf, const char* name)
{
    tracy::SourceLocationData srcloc = { name, "", "", 0, 0 };
    tracy::VkCtxScope scope(static_cast<tracy::VkCtx*>(ctx), &srcloc, cmdbuf, 6, true);
}

void tracy_vk_zone_c(TracyVkCtxHandle ctx, VkCommandBuffer cmdbuf, const char* name, uint32_t color)
{
    tracy::SourceLocationData srcloc = { name, "", "", 0, color };
    tracy::VkCtxScope scope(static_cast<tracy::VkCtx*>(ctx), &srcloc, cmdbuf, 6, true);
}

// extern "C"
