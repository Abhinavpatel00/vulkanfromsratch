
#include "types.h"

typedef u32 MatId;
typedef u32 ShaderKey;
typedef u64 PipelineKey;

typedef struct MaterialPBR
{
	float baseColor[4]; // rgba
	float emissive[3];
	float emissiveIntensity;
	float params0[4]; // metallic, roughness, ao, opacity
	float params1[4]; // ior, clearcoat, clearcoatRoughness, _pad
} MaterialPBR;        // size multiple of 16

typedef struct MaterialAsset
{
	MatId id;
	ShaderKey shader_key;
	MaterialPBR cpu_params;
	char const* textures[8];        // file paths or handles
	VkDescriptorSet descriptor_set; // set=1 descriptor set owned by this material instance
} MaterialAsset;
