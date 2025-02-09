#ifndef _BASE_PASS_CONSTANTS_
#define _BASE_PASS_CONSTANTS_

#include "ShaderInterop.h"

struct BasePassConstants
{
    Matrix m_WorldToClip;
    Matrix m_PrevWorldToClip;
	Matrix m_WorldToView;
	Vector4 m_Frustum;
	Vector2U m_HZBDimensions;
	float m_P00;
	float m_P11;
	float m_NearPlane;
	uint32_t m_CullingFlags;
	uint32_t m_DebugMode;
	uint32_t PAD0;
	Vector2U m_OutputResolution;
};

struct BasePassInstanceConstants
{
	Matrix m_WorldMatrix;
	Matrix m_PrevWorldMatrix;
	Vector4 m_BoundingSphere;
	uint32_t m_MeshDataIdx;
	uint32_t m_MaterialDataIdx;
	Vector2 PAD0;
};

#endif // #define _BASE_PASS_CONSTANTS_
