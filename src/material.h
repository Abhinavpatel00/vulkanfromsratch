// material.h
// C99 Vulkan material system: definitions + instances + descriptor management
// Dependencies (expected to be provided by the app):
//  - volk (or any Vulkan loader)
//  - SPIRV-Reflect (spirv_reflect.h)
//  - stb_image (for texture loading) - optional helper
//  - A Vulkan device, physical device, queue, and command pool from the host

// material.h
#ifndef MATERIAL_H
#define MATERIAL_H

#include "main.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

// Basic texture handle (VkImage, view, sampler) with a name and reference count
typedef struct Texture {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkSampler sampler;
    uint32_t width, height, mipLevels;
    char *name;        // optional name/ID
    uint32_t ref_count; // reference count
} Texture;

// Material reflection binding (for SPIRV-Reflect results)
typedef struct MaterialReflectionBinding {
    uint32_t set;           // descriptor set index
    uint32_t binding;       // binding number
    VkDescriptorType type;  // descriptor type
    uint32_t count;         // array count (1 if not an array)
    uint32_t vecSize;       // size in bytes for uniform buffers
    char *name;             // resource name
} MaterialReflectionBinding;

// Reflection summary (descriptor layouts, push constants, etc.)
typedef struct MaterialReflection {
    MaterialReflectionBinding *bindings; // array of bindings
    size_t binding_count;

    // Vulkan descriptor set layouts (one per set index, or VK_NULL_HANDLE if unused)
    VkDescriptorSetLayout *set_layouts; 
    uint32_t set_layout_count;

    // Push constant ranges (if any)
    VkPushConstantRange *push_ranges;
    uint32_t push_range_count;

    // Total size for CPU-side uniform data (all UBOs)
    size_t uniform_data_size;
} MaterialReflection;

// Shared material definition (pipeline layout and SPIR-V code)
typedef struct MaterialDefinition {
    char *name;
    uint32_t *vert_spv; size_t vert_spv_word_count;
    uint32_t *frag_spv; size_t frag_spv_word_count;
    MaterialReflection reflection;
    VkPipelineLayout pipeline_layout;
    VkSpecializationInfo *spec_info;
    uint32_t flags;
} MaterialDefinition;

// Per-instance material (descriptor sets, UBO data, bound textures)
typedef struct MaterialInstance {
    MaterialDefinition *def; 
    VkDescriptorSet *descriptor_sets;  // one descriptor set per used set
    void *uniform_data;        // CPU-side uniform data buffer
    size_t uniform_data_size;
    VkBuffer ubo_buffer;       // GPU UBO buffer
    VkDeviceMemory ubo_memory; // GPU UBO memory
    // bound_textures array is indexed by reflection.bindings indices (only valid for image bindings)
    Texture **bound_textures;  
    void *user_ptr;
} MaterialInstance;

// Texture manager (keeps loaded textures and reference counts)
typedef struct TextureManager {
    Texture **textures;     // array of Texture*
    size_t count, capacity;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkQueue transfer_queue;
    VkCommandPool transfer_cmd_pool;
} TextureManager;

// Material system context (device, descriptor pool, etc.)
typedef struct MaterialSystem {
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkDescriptorPool descriptor_pool;
    VkCommandPool cmd_pool;
    VkQueue transfer_queue;
    TextureManager tex_manager;
} MaterialSystem;

// Initialize/shutdown material system (creates descriptor pool)
int material_system_init(MaterialSystem *sys, VkDevice device, VkPhysicalDevice physical_device,
                         VkQueue transfer_queue, VkCommandPool cmd_pool);
void material_system_shutdown(MaterialSystem *sys);

// Create/destroy a material definition from SPIR-V bytecode
int material_definition_create_from_spv(MaterialSystem *sys, MaterialDefinition *out_def,
                                        const char *name,
                                        const uint32_t *vert_spv, size_t vert_wordcount,
                                        const uint32_t *frag_spv, size_t frag_wordcount);
void material_definition_destroy(MaterialSystem *sys, MaterialDefinition *def);

// Create/destroy a material instance (allocates descriptor sets, UBOs)
int material_instance_create(MaterialSystem *sys, MaterialDefinition *def, MaterialInstance *out_instance);
void material_instance_destroy(MaterialSystem *sys, MaterialInstance *inst);

// Update part of the instance's uniform data (CPU side)
void material_instance_update_uniform(MaterialSystem *sys, MaterialInstance *inst,
                                      size_t offset, const void *data, size_t size);

// Bind a texture to a particular set/binding
int material_instance_bind_texture(MaterialSystem *sys, MaterialInstance *inst,
                                   uint32_t set, uint32_t binding, Texture *tex);

// Flush descriptor sets (write UBOs and image samplers to GPU)
int material_instance_flush_descriptors(MaterialSystem *sys, MaterialInstance *inst);

// Bind the instance's descriptor sets and push constants to a command buffer
void material_bind_for_draw(MaterialSystem *sys, VkCommandBuffer cmd, 
                            MaterialInstance *inst, VkPipelineBindPoint bind_point);

// Texture manager API
int texture_manager_init(TextureManager *mgr, VkDevice device, VkPhysicalDevice phys,
                         VkQueue transfer_queue, VkCommandPool cmd_pool);
void texture_manager_shutdown(TextureManager *mgr);

// Load a texture from raw pixel data (with optional mipmap generation). 
// If name matches an existing texture, it is reused (ref_count++).
Texture *texture_manager_load_from_memory(TextureManager *mgr, const char *name,
                                          const unsigned char *pixels,
                                          int w, int h, int channels, int generate_mips);

// Release (destroy) a texture (decrement ref count, free if reaches zero)
void texture_destroy(TextureManager *mgr, Texture *t);

#endif // MATERIAL_H


