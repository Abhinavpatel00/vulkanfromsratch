/*
   material.c
   Implementation notes:
   - Uses SPIRV-Reflect to scan shader resources, build VkDescriptorSetLayout objects, and compute a pipeline layout.
   - Creates a descriptor pool with caps for some sets and types; for production you might use per-frame pools.
   - Provides a basic texture upload helper using staging buffers.
   - This file intentionally keeps implementation straightforward and portable. Optimizations like pooling UBOs,
     double-buffering per-frame updates, or descriptor set recycling are left as exercise.
*/
// Expected external dependencies
#include "../external/SPIRV-Reflect/spirv_reflect.h"
// material.c
#include "material.h"
#include <vulkan/vulkan.h>
#include <math.h>
#include <assert.h>
#include <string.h>

// Include SPIRV-Reflect for shader reflection
#include "../external/SPIRV-Reflect/spirv_reflect.h"

// Utility macros
#define MALLOC(sz) malloc(sz)
#define FREE(p) do { free(p); p = NULL; } while (0)

// Duplicate a C string
static char *string_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = (char*)MALLOC(n);
    memcpy(r, s, n);
    return r;
}

// Find a suitable memory type index on the physical device
static uint32_t find_memory_type_index(VkPhysicalDevice physical_device, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;  // invalid
}

// --- TextureManager implementation ---

int texture_manager_init(TextureManager *mgr, VkDevice device, VkPhysicalDevice phys,
                         VkQueue transfer_queue, VkCommandPool cmd_pool) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(*mgr));
    mgr->device = device;
    mgr->physical_device = phys;
    mgr->transfer_queue = transfer_queue;
    mgr->transfer_cmd_pool = cmd_pool;
    mgr->capacity = 8;
    mgr->textures = (Texture**)MALLOC(sizeof(Texture*) * mgr->capacity);
    mgr->count = 0;
    return 0;
}

// Shutdown: destroy and free all textures (ignoring ref counts)
void texture_manager_shutdown(TextureManager *mgr) {
    if (!mgr) return;
    for (size_t i = 0; i < mgr->count; ++i) {
        Texture *t = mgr->textures[i];
        if (!t) continue;
        if (t->sampler) vkDestroySampler(mgr->device, t->sampler, NULL);
        if (t->view) vkDestroyImageView(mgr->device, t->view, NULL);
        if (t->image) vkDestroyImage(mgr->device, t->image, NULL);
        if (t->memory) vkFreeMemory(mgr->device, t->memory, NULL);
        FREE(t->name);
        FREE(t);
    }
    FREE(mgr->textures);
    mgr->count = mgr->capacity = 0;
}

// Release a texture: decrement ref_count, and free if it reaches 0
void texture_destroy(TextureManager *mgr, Texture *t) {
    if (!mgr || !t) return;
    // Find texture in manager
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < mgr->count; ++i) {
        if (mgr->textures[i] == t) { idx = i; break; }
    }
    if (idx == SIZE_MAX) return;
    // Decrement reference count
    if (t->ref_count > 1) {
        t->ref_count--;
        return;
    }
    // Destroy Vulkan objects and free memory
    if (t->sampler) vkDestroySampler(mgr->device, t->sampler, NULL);
    if (t->view) vkDestroyImageView(mgr->device, t->view, NULL);
    if (t->image) vkDestroyImage(mgr->device, t->image, NULL);
    if (t->memory) vkFreeMemory(mgr->device, t->memory, NULL);
    FREE(t->name);
    FREE(t);
    // Remove from the array
    for (size_t i = idx+1; i < mgr->count; ++i) {
        mgr->textures[i-1] = mgr->textures[i];
    }
    mgr->count--;
}

// Load a texture from raw pixel data (RGBA etc.)
// If name is provided and a texture with that name exists, it is reused (ref_count++).
Texture *texture_manager_load_from_memory(TextureManager *mgr, const char *name,
                                          const unsigned char *pixels,
                                          int w, int h, int channels, int generate_mips) {
    if (!mgr || !pixels || w <= 0 || h <= 0) return NULL;
    // If name given, try to find an existing texture
    if (name) {
        for (size_t i = 0; i < mgr->count; ++i) {
            Texture *t = mgr->textures[i];
            if (t && t->name && strcmp(t->name, name) == 0) {
                t->ref_count++;
                return t;
            }
        }
    }
    // Determine Vulkan format from number of channels
    VkFormat format;
    if (channels == 4) {
        format = VK_FORMAT_R8G8B8A8_UNORM;
    } else if (channels == 3) {
        format = VK_FORMAT_R8G8B8_UNORM;
    } else if (channels == 2) {
        format = VK_FORMAT_R8G8_UNORM;
    } else {
        format = VK_FORMAT_R8_UNORM;
    }
    // Compute mipmap levels
    uint32_t mipLevels = 1;
    if (generate_mips) {
        uint32_t maxDim = (uint32_t)((w > h) ? w : h);
        mipLevels = (uint32_t)(floor(log2(maxDim)) + 1);
    }
    VkDeviceSize imageSize = (VkDeviceSize)w * h * channels;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    {
        VkBufferCreateInfo bufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = imageSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(mgr->device, &bufInfo, NULL, &stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(mgr->device, stagingBuffer, &memReq);
        VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReq.size;
        uint32_t memIndex = find_memory_type_index(mgr->physical_device, memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memIndex == UINT32_MAX) {
            vkDestroyBuffer(mgr->device, stagingBuffer, NULL);
            return NULL;
        }
        allocInfo.memoryTypeIndex = memIndex;
        vkAllocateMemory(mgr->device, &allocInfo, NULL, &stagingMemory);
        vkBindBufferMemory(mgr->device, stagingBuffer, stagingMemory, 0);
    }
    // Copy pixel data into staging buffer
    {
        void *data;
        vkMapMemory(mgr->device, stagingMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, (size_t)imageSize);
        vkUnmapMemory(mgr->device, stagingMemory);
    }
    // Create the Vulkan image
    VkImage textureImage;
    VkDeviceMemory textureMemory;
    {
        VkImageCreateInfo imgInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.extent.width = (uint32_t)w;
        imgInfo.extent.height = (uint32_t)h;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = mipLevels;
        imgInfo.arrayLayers = 1;
        imgInfo.format = format;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (generate_mips) {
            imgInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        vkCreateImage(mgr->device, &imgInfo, NULL, &textureImage);

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(mgr->device, textureImage, &memReq);
        VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReq.size;
        uint32_t memIndex = find_memory_type_index(mgr->physical_device, memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memIndex == UINT32_MAX) {
            vkDestroyImage(mgr->device, textureImage, NULL);
            vkDestroyBuffer(mgr->device, stagingBuffer, NULL);
            vkFreeMemory(mgr->device, stagingMemory, NULL);
            return NULL;
        }
        allocInfo.memoryTypeIndex = memIndex;
        vkAllocateMemory(mgr->device, &allocInfo, NULL, &textureMemory);
        vkBindImageMemory(mgr->device, textureImage, textureMemory, 0);
    }
    // Copy from staging buffer to image (and generate mipmaps if requested)
    {
        VkCommandBufferAllocateInfo cmdBufAlloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdBufAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAlloc.commandPool = mgr->transfer_cmd_pool;
        cmdBufAlloc.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(mgr->device, &cmdBufAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Transition image to TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = textureImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);

        // Copy buffer to image
        VkBufferImageCopy copyRegion = {0};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = (VkOffset3D){0, 0, 0};
        copyRegion.imageExtent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, textureImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (generate_mips) {
            int32_t mipWidth = w;
            int32_t mipHeight = h;
            for (uint32_t i = 1; i < mipLevels; ++i) {
                // Transition previous mip level to SRC for blit
                VkImageMemoryBarrier mipBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                mipBarrier.image = textureImage;
                mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                mipBarrier.subresourceRange.baseArrayLayer = 0;
                mipBarrier.subresourceRange.layerCount = 1;
                mipBarrier.subresourceRange.baseMipLevel = i - 1;
                mipBarrier.subresourceRange.levelCount = 1;
                mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 0, NULL, 0, NULL, 1, &mipBarrier);

                // Blit from mip i-1 to i
                VkImageBlit blit = {0};
                blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.srcOffsets[1] = (VkOffset3D){mipWidth, mipHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.dstOffsets[1] = (VkOffset3D){
                    mipWidth > 1 ? mipWidth/2 : 1,
                    mipHeight > 1 ? mipHeight/2 : 1,
                    1
                };
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(cmd,
                               textureImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit, VK_FILTER_LINEAR);

                // Transition this mip level to SHADER_READ_ONLY_OPTIMAL
                VkImageMemoryBarrier finishBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                finishBarrier.image = textureImage;
                finishBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                finishBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                finishBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                finishBarrier.subresourceRange.baseArrayLayer = 0;
                finishBarrier.subresourceRange.layerCount = 1;
                finishBarrier.subresourceRange.baseMipLevel = i;
                finishBarrier.subresourceRange.levelCount = 1;
                finishBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                finishBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                finishBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                finishBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, NULL, 0, NULL, 1, &finishBarrier);

                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }
        } else {
            // Single-level: transition to SHADER_READ_ONLY_OPTIMAL
            VkImageMemoryBarrier finalizeBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            finalizeBarrier.image = textureImage;
            finalizeBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            finalizeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            finalizeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            finalizeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            finalizeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            finalizeBarrier.subresourceRange.baseMipLevel = 0;
            finalizeBarrier.subresourceRange.levelCount = 1;
            finalizeBarrier.subresourceRange.baseArrayLayer = 0;
            finalizeBarrier.subresourceRange.layerCount = 1;
            finalizeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            finalizeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &finalizeBarrier);
        }

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(mgr->transfer_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(mgr->transfer_queue);
        vkFreeCommandBuffers(mgr->device, mgr->transfer_cmd_pool, 1, &cmd);
    }

    // Cleanup staging resources
    vkDestroyBuffer(mgr->device, stagingBuffer, NULL);
    vkFreeMemory(mgr->device, stagingMemory, NULL);

    // Create image view for the texture
    VkImageView view;
    {
        VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = textureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(mgr->device, &viewInfo, NULL, &view);
    }

    // Create a sampler with linear filtering and repeat addressing
    VkSampler sampler;
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(mgr->physical_device, &props);
        VkSamplerCreateInfo sampInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampInfo.magFilter = VK_FILTER_LINEAR;
        sampInfo.minFilter = VK_FILTER_LINEAR;
        sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampInfo.anisotropyEnable = VK_TRUE;
        sampInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;
        sampInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampInfo.unnormalizedCoordinates = VK_FALSE;
        sampInfo.compareEnable = VK_FALSE;
        sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampInfo.minLod = 0.0f;
        sampInfo.maxLod = (float)mipLevels;
        vkCreateSampler(mgr->device, &sampInfo, NULL, &sampler);
    }

    // Allocate and populate Texture struct
    Texture *t = (Texture*)MALLOC(sizeof(Texture));
    memset(t, 0, sizeof(*t));
    t->image = textureImage;
    t->view = view;
    t->memory = textureMemory;
    t->sampler = sampler;
    t->width = (uint32_t)w;
    t->height = (uint32_t)h;
    t->mipLevels = mipLevels;
    t->name = string_dup(name);
    t->ref_count = 1;

    // Add to manager array
    if (mgr->count >= mgr->capacity) {
        size_t newcap = mgr->capacity * 2;
        mgr->textures = (Texture**)realloc(mgr->textures, sizeof(Texture*) * newcap);
        mgr->capacity = newcap;
    }
    mgr->textures[mgr->count++] = t;
    return t;
}

// --- MaterialSystem init/shutdown ---

static VkDescriptorPool create_default_descriptor_pool(VkDevice device) {
    VkDescriptorPoolSize poolSizes[3];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;       poolSizes[0].descriptorCount = 128;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;poolSizes[1].descriptorCount = 256;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;       poolSizes[2].descriptorCount = 32;

    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 256;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = poolSizes;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &dpi, NULL, &pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pool;
}

int material_system_init(MaterialSystem *sys, VkDevice device, VkPhysicalDevice physical_device,
                         VkQueue transfer_queue, VkCommandPool cmd_pool) {
    if (!sys) return -1;
    memset(sys, 0, sizeof(*sys));
    sys->device = device;
    sys->physical_device = physical_device;
    sys->transfer_queue = transfer_queue;
    sys->cmd_pool = cmd_pool;

    sys->descriptor_pool = create_default_descriptor_pool(device);
    if (sys->descriptor_pool == VK_NULL_HANDLE) return -2;

    texture_manager_init(&sys->tex_manager, device, physical_device, transfer_queue, cmd_pool);
    return 0;
}

void material_system_shutdown(MaterialSystem *sys) {
    if (!sys) return;
    if (sys->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(sys->device, sys->descriptor_pool, NULL);
        sys->descriptor_pool = VK_NULL_HANDLE;
    }
    texture_manager_shutdown(&sys->tex_manager);
}

// --- Reflection helpers (SPIRV-Reflect) ---

// Reflect a SPIR-V module into MaterialReflection
static int reflect_spv_to_material_reflection(const uint32_t *spv, size_t word_count, MaterialReflection *out_ref) {
    if (!spv || !out_ref) return -1;
    SpvReflectShaderModule module;
    SpvReflectResult res = spvReflectCreateShaderModule(word_count * 4, spv, &module);
    if (res != SPV_REFLECT_RESULT_SUCCESS) return -2;

    // Descriptor bindings
    uint32_t binding_count = 0;
    res = spvReflectEnumerateDescriptorBindings(&module, &binding_count, NULL);
    if (res != SPV_REFLECT_RESULT_SUCCESS) { spvReflectDestroyShaderModule(&module); return -3; }
    SpvReflectDescriptorBinding **bindings = (SpvReflectDescriptorBinding**)MALLOC(sizeof(SpvReflectDescriptorBinding*) * binding_count);
    res = spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings);
    if (res != SPV_REFLECT_RESULT_SUCCESS) { FREE(bindings); spvReflectDestroyShaderModule(&module); return -4; }

    out_ref->bindings = (MaterialReflectionBinding*)MALLOC(sizeof(MaterialReflectionBinding) * binding_count);
    out_ref->binding_count = 0;
    for (uint32_t i = 0; i < binding_count; ++i) {
        SpvReflectDescriptorBinding *b = bindings[i];
        MaterialReflectionBinding *mb = &out_ref->bindings[out_ref->binding_count++];
        mb->set = b->set;
        mb->binding = b->binding;
        mb->count = (b->array.dims_count ? b->array.dims[0] : 1);
        // Map SPIRV-Reflect type to Vulkan type
        switch (b->descriptor_type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: mb->type = VK_DESCRIPTOR_TYPE_SAMPLER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: mb->type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: mb->type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: mb->type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: mb->type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: mb->type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: mb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: mb->type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: mb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: mb->type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC; break;
            default: mb->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
        }
    // In this SPIRV-Reflect version, 'block' is a struct (not a pointer)
    // Size will be 0 for non-block resources (e.g., images/samplers)
    mb->vecSize = b->block.size;
        mb->name = string_dup(b->name);
    }
    FREE(bindings);

    // Push constant ranges
    uint32_t push_count = 0;
    res = spvReflectEnumeratePushConstantBlocks(&module, &push_count, NULL);
    if (res == SPV_REFLECT_RESULT_SUCCESS && push_count > 0) {
        SpvReflectBlockVariable **blocks = (SpvReflectBlockVariable**)MALLOC(sizeof(SpvReflectBlockVariable*) * push_count);
        res = spvReflectEnumeratePushConstantBlocks(&module, &push_count, blocks);
        if (res == SPV_REFLECT_RESULT_SUCCESS) {
            out_ref->push_ranges = (VkPushConstantRange*)MALLOC(sizeof(VkPushConstantRange) * push_count);
            out_ref->push_range_count = push_count;
            VkShaderStageFlagBits stage = VK_SHADER_STAGE_ALL; // combined stage
            if (module.shader_stage & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) stage = VK_SHADER_STAGE_VERTEX_BIT;
            else if (module.shader_stage & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            else if (module.shader_stage & SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT) stage = VK_SHADER_STAGE_COMPUTE_BIT;
            for (uint32_t i = 0; i < push_count; ++i) {
                out_ref->push_ranges[i].stageFlags = stage;
                out_ref->push_ranges[i].offset = blocks[i]->offset;
                out_ref->push_ranges[i].size = blocks[i]->size;
            }
        }
        FREE(blocks);
    } else {
        out_ref->push_ranges = NULL;
        out_ref->push_range_count = 0;
    }

    spvReflectDestroyShaderModule(&module);
    return 0;
}

// Free a MaterialReflection
static void material_reflection_free(MaterialReflection *r) {
    if (!r) return;
    for (size_t i = 0; i < r->binding_count; ++i) {
        FREE(r->bindings[i].name);
    }
    FREE(r->bindings);
    if (r->set_layouts) { FREE(r->set_layouts); }
    if (r->push_ranges) { FREE(r->push_ranges); }
    memset(r, 0, sizeof(*r));
}

// Create VkDescriptorSetLayouts for each set in reflection
static int material_reflection_create_layouts(MaterialSystem *sys, MaterialReflection *r) {
    if (!sys || !r) return -1;
    uint32_t max_set = 0;
    for (size_t i = 0; i < r->binding_count; ++i) {
        max_set = (r->bindings[i].set > max_set) ? r->bindings[i].set : max_set;
    }
    uint32_t set_count = max_set + 1;
    r->set_layout_count = set_count;
    r->set_layouts = (VkDescriptorSetLayout*)MALLOC(sizeof(VkDescriptorSetLayout) * set_count);
    for (uint32_t s = 0; s < set_count; ++s) {
        r->set_layouts[s] = VK_NULL_HANDLE;
    }
    // Create layout for each set that has bindings
    for (uint32_t s = 0; s < set_count; ++s) {
        uint32_t count = 0;
        for (size_t i = 0; i < r->binding_count; ++i) {
            if (r->bindings[i].set == s) count++;
        }
        if (count == 0) continue;
        VkDescriptorSetLayoutBinding *vk_bindings = (VkDescriptorSetLayoutBinding*)MALLOC(sizeof(VkDescriptorSetLayoutBinding) * count);
        uint32_t bi = 0;
        for (size_t i = 0; i < r->binding_count; ++i) {
            if (r->bindings[i].set != s) continue;
            vk_bindings[bi].binding = r->bindings[i].binding;
            vk_bindings[bi].descriptorCount = r->bindings[i].count;
            vk_bindings[bi].descriptorType = r->bindings[i].type;
            vk_bindings[bi].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            vk_bindings[bi].pImmutableSamplers = NULL;
            bi++;
        }
        VkDescriptorSetLayoutCreateInfo dslci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslci.bindingCount = bi;
        dslci.pBindings = vk_bindings;
        if (vkCreateDescriptorSetLayout(sys->device, &dslci, NULL, &r->set_layouts[s]) != VK_SUCCESS) {
            for (uint32_t k = 0; k < s; ++k) {
                if (r->set_layouts[k] != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(sys->device, r->set_layouts[k], NULL);
                }
            }
            FREE(vk_bindings);
            FREE(r->set_layouts);
            return -2;
        }
        FREE(vk_bindings);
    }
    return 0;
}

// Create a Vulkan pipeline layout (descriptor sets + push constants)
static int material_reflection_create_pipeline_layout(MaterialSystem *sys, MaterialReflection *r, VkPipelineLayout *out_layout) {
    if (!sys || !r || !out_layout) return -1;
    // Collect non-null set layouts
    uint32_t count = 0;
    for (uint32_t i = 0; i < r->set_layout_count; ++i) {
        if (r->set_layouts[i] != VK_NULL_HANDLE) count++;
    }
    VkDescriptorSetLayout *layouts = (VkDescriptorSetLayout*)MALLOC(sizeof(VkDescriptorSetLayout) * count);
    uint32_t idx = 0;
    for (uint32_t i = 0; i < r->set_layout_count; ++i) {
        if (r->set_layouts[i] != VK_NULL_HANDLE) {
            layouts[idx++] = r->set_layouts[i];
        }
    }
    VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = count;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = r->push_range_count;
    plci.pPushConstantRanges = r->push_ranges;
    VkResult res = vkCreatePipelineLayout(sys->device, &plci, NULL, out_layout);
    FREE(layouts);
    return (res == VK_SUCCESS) ? 0 : -1;
}

// Create a MaterialDefinition from SPIR-V shaders
int material_definition_create_from_spv(MaterialSystem *sys, MaterialDefinition *out_def,
                                       const char *name,
                                       const uint32_t *vert_spv, size_t vert_wordcount,
                                       const uint32_t *frag_spv, size_t frag_wordcount) {
    if (!sys || !out_def || (!vert_spv && !frag_spv)) return -1;
    memset(out_def, 0, sizeof(*out_def));
    out_def->name = string_dup(name);

    MaterialReflection combined;
    memset(&combined, 0, sizeof(combined));
    MaterialReflection vert_ref;
    memset(&vert_ref, 0, sizeof(vert_ref));
    MaterialReflection frag_ref;
    memset(&frag_ref, 0, sizeof(frag_ref));

    // Reflect fragment shader if provided
    if (frag_spv) {
        reflect_spv_to_material_reflection(frag_spv, frag_wordcount, &frag_ref);
        combined.bindings = frag_ref.bindings;
        combined.binding_count = frag_ref.binding_count;
        combined.push_ranges = frag_ref.push_ranges;
        combined.push_range_count = frag_ref.push_range_count;
    }
    // Reflect vertex shader and merge bindings/push constants
    if (vert_spv) {
        reflect_spv_to_material_reflection(vert_spv, vert_wordcount, &vert_ref);
        // Merge bindings: skip duplicates (same set/binding)
        size_t total = combined.binding_count + vert_ref.binding_count;
        MaterialReflectionBinding *merged = (MaterialReflectionBinding*)MALLOC(sizeof(MaterialReflectionBinding) * total);
        size_t k = 0;
        for (size_t i = 0; i < combined.binding_count; ++i) {
            merged[k++] = combined.bindings[i];
        }
        for (size_t i = 0; i < vert_ref.binding_count; ++i) {
            int exists = 0;
            for (size_t j = 0; j < combined.binding_count; ++j) {
                if (vert_ref.bindings[i].set == combined.bindings[j].set &&
                    vert_ref.bindings[i].binding == combined.bindings[j].binding) {
                    exists = 1;
                    break;
                }
            }
            if (!exists) {
                merged[k++] = vert_ref.bindings[i];
            } else {
                FREE(vert_ref.bindings[i].name);
            }
        }
        size_t merged_count = k;
        FREE(combined.bindings);
        FREE(vert_ref.bindings);
        combined.bindings = merged;
        combined.binding_count = merged_count;

        // Merge push constants
        if (combined.push_range_count > 0 || vert_ref.push_range_count > 0) {
            uint32_t totalPC = combined.push_range_count + vert_ref.push_range_count;
            VkPushConstantRange *pc_merged = (VkPushConstantRange*)MALLOC(sizeof(VkPushConstantRange) * totalPC);
            uint32_t m = 0;
            for (uint32_t i = 0; i < combined.push_range_count; ++i) {
                pc_merged[m++] = combined.push_ranges[i];
            }
            for (uint32_t i = 0; i < vert_ref.push_range_count; ++i) {
                pc_merged[m++] = vert_ref.push_ranges[i];
            }
            FREE(combined.push_ranges);
            FREE(vert_ref.push_ranges);
            combined.push_ranges = pc_merged;
            combined.push_range_count = totalPC;
        }
    }

    // Compute total UBO size
    size_t ubytes = 0;
    for (size_t i = 0; i < combined.binding_count; ++i) {
        VkDescriptorType type = combined.bindings[i].type;
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            ubytes += combined.bindings[i].vecSize;
        }
    }
    combined.uniform_data_size = ubytes;

    // Create descriptor set layouts
    if (material_reflection_create_layouts(sys, &combined) != 0) {
        material_reflection_free(&combined);
        return -2;
    }
    // Create pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (material_reflection_create_pipeline_layout(sys, &combined, &pipelineLayout) != 0) {
        for (uint32_t i = 0; i < combined.set_layout_count; ++i) {
            if (combined.set_layouts[i] != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(sys->device, combined.set_layouts[i], NULL);
            }
        }
        material_reflection_free(&combined);
        return -3;
    }

    out_def->vert_spv = (uint32_t*)vert_spv; out_def->vert_spv_word_count = vert_wordcount;
    out_def->frag_spv = (uint32_t*)frag_spv; out_def->frag_spv_word_count = frag_wordcount;
    out_def->reflection = combined;
    out_def->pipeline_layout = pipelineLayout;
    return 0;
}

void material_definition_destroy(MaterialSystem *sys, MaterialDefinition *def) {
    if (!sys || !def) return;
    if (def->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(sys->device, def->pipeline_layout, NULL);
        def->pipeline_layout = VK_NULL_HANDLE;
    }
    if (def->reflection.set_layouts) {
        for (uint32_t i = 0; i < def->reflection.set_layout_count; ++i) {
            if (def->reflection.set_layouts[i] != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(sys->device, def->reflection.set_layouts[i], NULL);
            }
        }
    }
    material_reflection_free(&def->reflection);
    FREE(def->name);
    memset(def, 0, sizeof(*def));
}

// --- MaterialInstance (per-material) ---

int material_instance_create(MaterialSystem *sys, MaterialDefinition *def, MaterialInstance *out_instance) {
    if (!sys || !def || !out_instance) return -1;
    memset(out_instance, 0, sizeof(*out_instance));
    out_instance->def = def;
    size_t set_count = def->reflection.set_layout_count;
    if (set_count == 0) return -2;
    out_instance->descriptor_sets = (VkDescriptorSet*)MALLOC(sizeof(VkDescriptorSet) * set_count);
    for (size_t i = 0; i < set_count; ++i) {
        out_instance->descriptor_sets[i] = VK_NULL_HANDLE;
    }
    // Allocate descriptor sets for each non-null layout
    uint32_t alloc_count = 0;
    for (uint32_t i = 0; i < set_count; ++i) {
        if (def->reflection.set_layouts[i] != VK_NULL_HANDLE) alloc_count++;
    }
    if (alloc_count > 0) {
        VkDescriptorSetLayout *layouts = (VkDescriptorSetLayout*)MALLOC(sizeof(VkDescriptorSetLayout) * alloc_count);
        uint32_t *layout_indices = (uint32_t*)MALLOC(sizeof(uint32_t) * alloc_count);
        uint32_t idx = 0;
        for (uint32_t i = 0; i < set_count; ++i) {
            if (def->reflection.set_layouts[i] == VK_NULL_HANDLE) continue;
            layouts[idx] = def->reflection.set_layouts[i];
            layout_indices[idx] = i;
            idx++;
        }
        VkDescriptorSetAllocateInfo dsai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsai.descriptorPool = sys->descriptor_pool;
        dsai.descriptorSetCount = alloc_count;
        dsai.pSetLayouts = layouts;
        VkDescriptorSet *sets = (VkDescriptorSet*)MALLOC(sizeof(VkDescriptorSet) * alloc_count);
        if (vkAllocateDescriptorSets(sys->device, &dsai, sets) != VK_SUCCESS) {
            FREE(out_instance->descriptor_sets);
            FREE(layouts);
            FREE(layout_indices);
            FREE(sets);
            return -3;
        }
        // Assign to correct set index
        for (uint32_t i = 0; i < alloc_count; ++i) {
            out_instance->descriptor_sets[layout_indices[i]] = sets[i];
        }
        FREE(layouts);
        FREE(layout_indices);
        FREE(sets);
    }
    // Allocate CPU uniform data if needed
    if (def->reflection.uniform_data_size > 0) {
        out_instance->uniform_data_size = def->reflection.uniform_data_size;
        out_instance->uniform_data = MALLOC(out_instance->uniform_data_size);
        memset(out_instance->uniform_data, 0, out_instance->uniform_data_size);
        // Create GPU buffer (host visible for simplicity)
        VkBufferCreateInfo bufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = out_instance->uniform_data_size;
        bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(sys->device, &bufInfo, NULL, &out_instance->ubo_buffer) != VK_SUCCESS) {
            return -4;
        }
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(sys->device, out_instance->ubo_buffer, &memReq);
        VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReq.size;
        uint32_t idx = find_memory_type_index(sys->physical_device, memReq.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) {
            return -5;
        }
        allocInfo.memoryTypeIndex = idx;
        if (vkAllocateMemory(sys->device, &allocInfo, NULL, &out_instance->ubo_memory) != VK_SUCCESS) {
            return -6;
        }
        vkBindBufferMemory(sys->device, out_instance->ubo_buffer, out_instance->ubo_memory, 0);
    }
    // Allocate array for bound textures (one slot per reflected binding)
    size_t bcount = def->reflection.binding_count;
    out_instance->bound_textures = (Texture**)MALLOC(sizeof(Texture*) * bcount);
    for (size_t i = 0; i < bcount; ++i) {
        out_instance->bound_textures[i] = NULL;
    }
    return 0;
}

// Update part of the CPU uniform data
void material_instance_update_uniform(MaterialSystem *sys, MaterialInstance *inst,
                                      size_t offset, const void *data, size_t size) {
    if (!inst || !data) return;
    if (offset + size > inst->uniform_data_size) return;
    memcpy((char*)inst->uniform_data + offset, data, size);
}

// Find index in reflection.bindings for given set and binding
static int find_ref_binding_index(MaterialDefinition *def, uint32_t set, uint32_t binding) {
    for (size_t i = 0; i < def->reflection.binding_count; ++i) {
        if (def->reflection.bindings[i].set == set &&
            def->reflection.bindings[i].binding == binding) {
            return (int)i;
        }
    }
    return -1;
}

// Bind a texture to a set/binding
int material_instance_bind_texture(MaterialSystem *sys, MaterialInstance *inst,
                                   uint32_t set, uint32_t binding, Texture *tex) {
    if (!inst) return -1;
    MaterialDefinition *def = inst->def;
    int idx = find_ref_binding_index(def, set, binding);
    if (idx < 0) return -2;
    MaterialReflectionBinding *b = &def->reflection.bindings[idx];
    if (b->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
        b->type != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
        return -3;
    }
    inst->bound_textures[idx] = tex;
    return 0;
}

// Flush descriptors (write uniform buffers and combined image samplers)
int material_instance_flush_descriptors(MaterialSystem *sys, MaterialInstance *inst) {
    if (!sys || !inst) return -1;
    MaterialDefinition *def = inst->def;
    size_t maxWrites = def->reflection.binding_count;
    VkWriteDescriptorSet *writes = (VkWriteDescriptorSet*)MALLOC(sizeof(VkWriteDescriptorSet) * maxWrites);
    VkDescriptorImageInfo *imageInfos = (VkDescriptorImageInfo*)MALLOC(sizeof(VkDescriptorImageInfo) * maxWrites);
    VkDescriptorBufferInfo *bufInfos = (VkDescriptorBufferInfo*)MALLOC(sizeof(VkDescriptorBufferInfo) * maxWrites);
    size_t wcount = 0;
    // Copy CPU uniform data to GPU buffer, if present
    if (inst->ubo_buffer != VK_NULL_HANDLE && inst->uniform_data) {
        void *mapped = NULL;
        vkMapMemory(sys->device, inst->ubo_memory, 0, inst->uniform_data_size, 0, &mapped);
        memcpy(mapped, inst->uniform_data, inst->uniform_data_size);
        vkUnmapMemory(sys->device, inst->ubo_memory);
    }
    for (size_t i = 0; i < def->reflection.binding_count; ++i) {
        MaterialReflectionBinding *b = &def->reflection.bindings[i];
        if (b->set >= def->reflection.set_layout_count) continue;
        VkDescriptorSet ds = inst->descriptor_sets[b->set];
        if (ds == VK_NULL_HANDLE) continue;
        if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
            b->type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            Texture *t = inst->bound_textures[i];
            if (!t) continue;
            imageInfos[wcount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[wcount].imageView = t->view;
            imageInfos[wcount].sampler = t->sampler;
            writes[wcount] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[wcount].dstSet = ds;
            writes[wcount].dstBinding = b->binding;
            writes[wcount].dstArrayElement = 0;
            writes[wcount].descriptorCount = 1;
            writes[wcount].descriptorType = b->type;
            writes[wcount].pImageInfo = &imageInfos[wcount];
            writes[wcount].pBufferInfo = NULL;
            writes[wcount].pTexelBufferView = NULL;
            wcount++;
        }
        else if (b->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 b->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            if (inst->ubo_buffer == VK_NULL_HANDLE) continue;
            bufInfos[wcount].buffer = inst->ubo_buffer;
            bufInfos[wcount].offset = 0;
            bufInfos[wcount].range = b->vecSize;
            writes[wcount] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[wcount].dstSet = ds;
            writes[wcount].dstBinding = b->binding;
            writes[wcount].dstArrayElement = 0;
            writes[wcount].descriptorCount = 1;
            writes[wcount].descriptorType = b->type;
            writes[wcount].pImageInfo = NULL;
            writes[wcount].pBufferInfo = &bufInfos[wcount];
            writes[wcount].pTexelBufferView = NULL;
            wcount++;
        }
    }
    if (wcount > 0) {
        vkUpdateDescriptorSets(sys->device, (uint32_t)wcount, writes, 0, NULL);
    }
    FREE(writes);
    FREE(imageInfos);
    FREE(bufInfos);
    return 0;
}

// Bind descriptor sets (and push constants) before drawing
void material_bind_for_draw(MaterialSystem *sys, VkCommandBuffer cmd, MaterialInstance *inst, VkPipelineBindPoint bind_point) {
    if (!sys || !inst || !cmd) return;
    MaterialDefinition *def = inst->def;
    uint32_t cnt = 0;
    VkDescriptorSet *sets = (VkDescriptorSet*)MALLOC(sizeof(VkDescriptorSet) * def->reflection.set_layout_count);
    for (uint32_t i = 0; i < def->reflection.set_layout_count; ++i) {
        if (inst->descriptor_sets[i] != VK_NULL_HANDLE) {
            sets[cnt++] = inst->descriptor_sets[i];
        }
    }
    if (cnt > 0) {
        vkCmdBindDescriptorSets(cmd, bind_point, def->pipeline_layout, 0, cnt, sets, 0, NULL);
    }
    FREE(sets);
    // (Push constants would be written here if needed)
}

// Destroy a MaterialInstance (free descriptor sets and buffers)
void material_instance_destroy(MaterialSystem *sys, MaterialInstance *inst) {
    if (!sys || !inst) return;
    if (inst->descriptor_sets) {
        for (uint32_t i = 0; i < inst->def->reflection.set_layout_count; ++i) {
            if (inst->descriptor_sets[i] != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(sys->device, sys->descriptor_pool, 1, &inst->descriptor_sets[i]);
            }
        }
        FREE(inst->descriptor_sets);
    }
    if (inst->uniform_data) {
        FREE(inst->uniform_data);
    }
    if (inst->ubo_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(sys->device, inst->ubo_buffer, NULL);
    }
    if (inst->ubo_memory != VK_NULL_HANDLE) {
        vkFreeMemory(sys->device, inst->ubo_memory, NULL);
    }
    if (inst->bound_textures) {
        FREE(inst->bound_textures);
    }
    memset(inst, 0, sizeof(*inst));
}

