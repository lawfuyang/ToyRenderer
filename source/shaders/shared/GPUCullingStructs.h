#ifndef _GPU_CULLING_STRUCTS_H_
#define _GPU_CULLING_STRUCTS_H_

#include "ShaderInterop.h"

struct GPUCullingPassConstants
{
	uint32_t m_NbInstances;
	uint32_t m_CullingFlags;
	Vector2U m_HZBDimensions;
	Vector4 m_Frustum;
	Matrix m_ViewMatrix;
	Matrix m_PrevViewMatrix;
	float m_NearPlane;
	float m_P00;
    float m_P11;
};

#endif
