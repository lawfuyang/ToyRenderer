#ifndef _MATERIAL_DATA_H_
#define _MATERIAL_DATA_H_

#include "StructsCommon.h"

struct MaterialData
{
	Vector4 m_ConstAlbedo;
	Vector3 m_ConstEmissive;
	float m_AlphaCutoff;
	uint32_t m_AlbedoTextureSamplerAndDescriptorIndex;
	uint32_t m_NormalTextureSamplerAndDescriptorIndex;
	uint32_t m_MetallicRoughnessTextureSamplerAndDescriptorIndex;
	uint32_t m_EmissiveTextureSamplerAndDescriptorIndex;
	Vector4 m_AlbedoUVOffsetAndScale;
	Vector4 m_NormalUVOffsetAndScale;
	Vector4 m_MetallicRoughnessUVOffsetAndScale;
	Vector4 m_EmissiveUVOffsetAndScale;
	uint32_t m_MaterialFlags;
	float m_ConstRoughness;
	float m_ConstMetallic;
};

#endif // _MATERIAL_DATA_H_
