

#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H


#include "../external/volk/volk.h"

#define MAX_DESCRIPTOR_BINDINGS 16

#include <stdint.h>

// --- Binding description ---
typedef struct DescriptorBinding {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    VkShaderStageFlags stages;
} DescriptorBinding;

// --- Layout description ---
typedef struct DescriptorSetLayoutDesc {
    const DescriptorBinding* bindings;
    uint32_t bindingCount;
} DescriptorSetLayoutDesc;

typedef struct DescriptorSetLayout {
    VkDescriptorSetLayout handle;
    DescriptorBinding* bindings;  // stored copy
    uint32_t bindingCount;
} DescriptorSetLayout;

// --- Pool allocator ---
typedef struct DescriptorAllocator {
    VkDescriptorPool pool;
} DescriptorAllocator;

// --- Descriptor set ---
typedef struct DescriptorSet {
    VkDescriptorSet handle;
    DescriptorSetLayout* layout;
} DescriptorSet;

// --- Write description ---
typedef struct DescriptorWrite {
    uint32_t binding;
    uint32_t arrayElement;
    VkDescriptorType type;
    VkDescriptorImageInfo imageInfo;
    VkDescriptorBufferInfo bufferInfo;
} DescriptorWrite;

// --- Functions ---

// Layout
VkResult create_descriptor_set_layout(VkDevice device, const DescriptorSetLayoutDesc* desc, DescriptorSetLayout* out);
void destroy_descriptor_set_layout(VkDevice device, DescriptorSetLayout* layout);

// Allocator
VkResult create_descriptor_allocator(VkDevice device, const VkDescriptorPoolSize* poolSizes, uint32_t poolSizeCount, uint32_t maxSets, DescriptorAllocator* out);
void destroy_descriptor_allocator(VkDevice device, DescriptorAllocator* allocator);

// Allocate set
VkResult allocate_descriptor_set(VkDevice device, DescriptorAllocator* allocator, DescriptorSetLayout* layout, DescriptorSet* out);

// Update
void update_descriptor_set(VkDevice device, DescriptorSet* set, const DescriptorWrite* writes, uint32_t writeCount);

// Bind
void bind_descriptor_set(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t setIndex, const DescriptorSet* set);

#endif // DESCRIPTOR_H

