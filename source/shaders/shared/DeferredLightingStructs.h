#ifndef _DEFERRED_LIGHTING_CONSTANTS_
#define _DEFERRED_LIGHTING_CONSTANTS_

#include "StructsCommon.h"

static const uint32_t kDeferredLightingDebugFlag_LightingOnly      = (1 << 0);
static const uint32_t kDeferredLightingDebugFlag_ColorizeInstances = (1 << 1);

struct DeferredLightingConsts
{
	Matrix m_InvViewProjMatrix;
	Vector3 m_CameraOrigin;
	uint32_t m_SSAOEnabled;
	Vector3 m_DirectionalLightColor;
	float m_DirectionalLightStrength;
	Vector3 m_DirectionalLightVector;
	float m_InvShadowMapResolution;
	Vector4 m_CSMDistances; // for some reason, this can't be an array of 4 floats. as it will have 16 byte offsets per element?!?!?!?
	Matrix m_DirLightViewProj[4];
	Vector2U m_LightingOutputResolution;
	uint32_t m_DebugFlags;
};

#endif // #ifndef _DEFERRED_LIGHTING_CONSTANTS_
