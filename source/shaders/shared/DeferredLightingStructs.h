#ifndef _DEFERRED_LIGHTING_CONSTANTS_
#define _DEFERRED_LIGHTING_CONSTANTS_

#include "ShaderInterop.h"

static const uint32_t kDeferredLightingDebugMode_LightingOnly      = 1;
static const uint32_t kDeferredLightingDebugMode_ColorizeInstances = 2;
static const uint32_t kDeferredLightingDebugMode_Albedo            = 3;
static const uint32_t kDeferredLightingDebugMode_Normal            = 4;
static const uint32_t kDeferredLightingDebugMode_Emissive          = 5;
static const uint32_t kDeferredLightingDebugMode_Metalness         = 6;
static const uint32_t kDeferredLightingDebugMode_Roughness         = 7;
static const uint32_t kDeferredLightingDebugMode_AmbientOcclusion  = 8;
static const uint32_t kDeferredLightingDebugMode_Ambient           = 9;
static const uint32_t kDeferredLightingDebugMode_ShadowMask        = 10;

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
	uint32_t m_DebugMode;
};

#endif // #ifndef _DEFERRED_LIGHTING_CONSTANTS_
