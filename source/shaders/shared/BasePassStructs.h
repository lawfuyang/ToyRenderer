#ifndef _BASE_PASS_CONSTANTS_
#define _BASE_PASS_CONSTANTS_

#include "StructsCommon.h"

struct BasePassConstants
{
    Matrix m_ViewProjMatrix;
    Vector3 m_DirectionalLightVector;
	uint32_t m_InstanceConstIdx; // for non-instanced rendering
    Vector3 m_DirectionalLightColor;
	float m_InvShadowMapResolution;
	Vector3 m_CameraOrigin;
	uint32_t m_SSAOEnabled;
	Vector4 m_CSMDistances; // for some reason, this can't be an array of 4 floats. as it will have 16 byte offsets per element?!?!?!?
	Matrix m_DirLightViewProj[4];
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
