#ifndef _MATERIAL_DATA_H_
#define _MATERIAL_DATA_H_

#include "StructsCommon.h"

struct MaterialData
{
	Vector3 m_ConstDiffuse;
	uint32_t m_MaterialFlags;
	uint32_t m_AlbedoTextureSamplerAndDescriptorIndex;
	uint32_t m_NormalTextureSamplerAndDescriptorIndex;
	uint32_t m_MetallicRoughnessTextureSamplerAndDescriptorIndex;
	float m_ConstRoughness;
	float m_ConstMetallic;
};

#endif // _MATERIAL_DATA_H_
