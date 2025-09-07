#include "descriptor.h"
#include <stdlib.h>
#include <string.h>
///A descriptor set is basically a collection of descriptors (handles to buffers/images/samplers) that must match what your shader expects

// avoid creating too much structs 
// we need to have an arena allcator for efffective memory allocation  and cache usage 
// well i will not use iti will rewtite this fileand rethink aboyt it 

// --- Layout ---
 VkResult create_descriptor_set_layout(VkDevice device, const DescriptorSetLayoutDesc* desc, DescriptorSetLayout* out) {
    VkDescriptorSetLayoutBinding* vkBindings = malloc(sizeof(VkDescriptorSetLayoutBinding) * desc->bindingCount);
    if (!vkBindings) return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (uint32_t i = 0; i < desc->bindingCount; i++) {
        const DescriptorBinding* b = &desc->bindings[i];
        vkBindings[i] = (VkDescriptorSetLayoutBinding) {
            .binding = b->binding,
            .descriptorType = b->type,
            .descriptorCount = b->count,
            .stageFlags = b->stages,
            .pImmutableSamplers = NULL
        };
    }

    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc->bindingCount,
        .pBindings = vkBindings
    };

    VkResult res = vkCreateDescriptorSetLayout(device, &info, NULL, &out->handle);
    if (res == VK_SUCCESS) {
        out->bindingCount = desc->bindingCount;
        out->bindings = malloc(sizeof(DescriptorBinding) * desc->bindingCount);
        memcpy(out->bindings, desc->bindings, sizeof(DescriptorBinding) * desc->bindingCount);
    }

    free(vkBindings);
    return res;
}

void destroy_descriptor_set_layout(VkDevice device, DescriptorSetLayout* layout) {
    if (layout->handle) {
        vkDestroyDescriptorSetLayout(device, layout->handle, NULL);
        layout->handle = VK_NULL_HANDLE;
    }
    free(layout->bindings);
    layout->bindings = NULL;
    layout->bindingCount = 0;
}

// --- Allocator ---
VkResult create_descriptor_allocator(VkDevice device, const VkDescriptorPoolSize* poolSizes, uint32_t poolSizeCount, uint32_t maxSets, DescriptorAllocator* out) {
    VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = maxSets,
        .poolSizeCount = poolSizeCount,
        .pPoolSizes = poolSizes
    };

    return vkCreateDescriptorPool(device, &info, NULL, &out->pool);
}

void destroy_descriptor_allocator(VkDevice device, DescriptorAllocator* allocator) {
    if (allocator->pool) {
        vkDestroyDescriptorPool(device, allocator->pool, NULL);
        allocator->pool = VK_NULL_HANDLE;
    }
}

// --- Allocate set ---
VkResult allocate_descriptor_set(VkDevice device, DescriptorAllocator* allocator, DescriptorSetLayout* layout, DescriptorSet* out) {
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = allocator->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout->handle
    };

    VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &out->handle);
    if (res == VK_SUCCESS) {
        out->layout = layout;
    }
    return res;
}

// --- Update ---
void update_descriptor_set(VkDevice device, DescriptorSet* set, const DescriptorWrite* writes, uint32_t writeCount) {
    VkWriteDescriptorSet* vkWrites = malloc(sizeof(VkWriteDescriptorSet) * writeCount);

    for (uint32_t i = 0; i < writeCount; i++) {
        vkWrites[i] = (VkWriteDescriptorSet) {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set->handle,
            .dstBinding = writes[i].binding,
            .dstArrayElement = writes[i].arrayElement,
            .descriptorCount = 1,
            .descriptorType = writes[i].type,
            .pImageInfo = writes[i].type == VK_DESCRIPTOR_TYPE_SAMPLER ||
                          writes[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                          writes[i].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                          writes[i].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                          ? &writes[i].imageInfo : NULL,
            .pBufferInfo = writes[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                           writes[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                           writes[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                           writes[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                           ? &writes[i].bufferInfo : NULL,
            .pTexelBufferView = NULL
        };
    }

    vkUpdateDescriptorSets(device, writeCount, vkWrites, 0, NULL);
    free(vkWrites);
}

// --- Bind ---
void bind_descriptor_set(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t setIndex, const DescriptorSet* set) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, setIndex, 1, &set->handle, 0, NULL);
}

