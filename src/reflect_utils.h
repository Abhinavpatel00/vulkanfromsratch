#ifndef REFLECT_UTILS_H
#define REFLECT_UTILS_H

#include "main.h"
#include <stdbool.h>

// Information about a descriptor binding discovered via reflection
typedef struct ReflectBindingInfo {
    uint32_t set;
    uint32_t binding;
    VkDescriptorType descriptorType;
    uint32_t descriptorCount; // array size
    const char* name;         // may be NULL
    VkShaderStageFlagBits stage; // stage the SPIR-V belongs to
} ReflectBindingInfo;

// Resolver callback: given a binding, fill one or more VkWriteDescriptorSet entries.
// You can write up to 'maxWrites' entries into outWrites, and use imageInfos/bufferInfos
// as backing storage for pImageInfo/pBufferInfo for those writes.
// Return number of writes produced. Return 0 to skip (leave unbound).
typedef uint32_t (*ReflectResourceResolver)(
    const ReflectBindingInfo* info,
    VkWriteDescriptorSet* outWrites, uint32_t maxWrites,
    VkDescriptorImageInfo* imageInfos, uint32_t maxImageInfos,
    VkDescriptorBufferInfo* bufferInfos, uint32_t maxBufferInfos);

// Aggregated outputs from reflection
typedef struct ReflectedDescriptors {
    VkDescriptorPool pool;
    VkDescriptorSetLayout* setLayouts; // array [setLayoutCount]
    uint32_t setLayoutCount;
    VkDescriptorSet* sets;             // array [setLayoutCount]
    VkPipelineLayout pipelineLayout;
} ReflectedDescriptors;

// Parse SPIR-V and create set layouts, pool, allocate sets, and (optionally) update them via resolver.
// - device: Vulkan device
// - spirv: pointer to SPIR-V binary
// - sizeBytes: SPIR-V size in bytes
// - out: filled with created descriptors data
// - resolver: may be NULL; if non-NULL, will be called for each binding to provide resources
//   If NULL, no vkUpdateDescriptorSets is called.
VkResult reflect_build_descriptors_from_spirv(
    VkDevice device,
    const void* spirv, size_t sizeBytes,
    ReflectedDescriptors* out,
    ReflectResourceResolver resolver);

// Destroy resources created in ReflectedDescriptors (does not destroy resources the resolver created).
void reflect_destroy(VkDevice device, ReflectedDescriptors* rd);

// Convenience: returns a simple resolver that binds every STORAGE_IMAGE to the given imageView/layout.
// Buffer bindings are ignored (not written).
ReflectResourceResolver reflect_make_storage_image_resolver(VkImageView imageView, VkImageLayout layout);

#endif // REFLECT_UTILS_H
