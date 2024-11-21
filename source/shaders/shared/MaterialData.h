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
	Vector2 m_AlbedoUVOffset;
    Vector2 m_AlbedoUVScale;
    Vector2 m_NormalUVOffset;
    Vector2 m_NormalUVScale;
    Vector2 m_MetallicRoughnessUVOffset;
    Vector2 m_MetallicRoughnessUVScale;
	float m_AlphaCutoff;
};

#endif // _MATERIAL_DATA_H_
