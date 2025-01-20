#ifndef _BASE_PASS_CONSTANTS_
#define _BASE_PASS_CONSTANTS_

#include "ShaderInterop.h"

struct BasePassConstants
{
    Matrix m_ViewProjMatrix;
	Matrix m_ViewMatrix;
	Vector4 m_Frustum;
	Vector2U m_HZBDimensions;
	float m_P00;
	float m_P11;
	float m_NearPlane;
	uint32_t m_CullingFlags;
	uint32_t m_DebugMode;
};

struct BasePassInstanceConstants
{
	Matrix m_WorldMatrix;
	Vector4 m_BoundingSphere;
	uint32_t m_MeshDataIdx;
	uint32_t m_MaterialDataIdx;
	Vector2 PAD0;
};

#endif // #define _BASE_PASS_CONSTANTS_
