#ifndef _BASE_PASS_CONSTANTS_
#define _BASE_PASS_CONSTANTS_

#include "StructsCommon.h"

struct BasePassConstants
{
    Matrix m_ViewProjMatrix;
	Matrix m_ViewMatrix;
    Vector3 m_DirectionalLightVector;
	uint32_t m_InstanceConstIdx; // for non-instanced rendering
    Vector3 m_DirectionalLightColor;
	float m_InvShadowMapResolution;
	Vector3 m_CameraOrigin;
	uint32_t m_SSAOEnabled;
	Vector4 m_CSMDistances; // for some reason, this can't be an array of 4 floats. as it will have 16 byte offsets per element?!?!?!?
	Matrix m_DirLightViewProj[4];

	// temp meshlet stuff until we move it all to indirect GPU Culling
	Vector4 m_Frustum;
	uint32_t m_EnableFrustumCulling;
	uint32_t m_bEnableMeshletConeCulling;
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
