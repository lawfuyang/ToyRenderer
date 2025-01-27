#ifndef _DEFERRED_LIGHTING_CONSTANTS_
#define _DEFERRED_LIGHTING_CONSTANTS_

#include "ShaderInterop.h"

struct DeferredLightingConsts
{
	Matrix m_InvViewProjMatrix;
	Vector3 m_CameraOrigin;
	uint32_t m_SSAOEnabled;
	Vector3 m_DirectionalLightColor;
	float m_DirectionalLightStrength;
	Vector3 m_DirectionalLightVector;
	uint32_t m_DebugMode;
	Vector2U m_LightingOutputResolution;
};

#endif // #ifndef _DEFERRED_LIGHTING_CONSTANTS_
