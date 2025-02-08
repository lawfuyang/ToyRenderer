#ifndef _SHADOW_MASK_STRUCTS_
#define _SHADOW_MASK_STRUCTS_

#include "ShaderInterop.h"

struct ShadowMaskConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_DirectionalLightDirection;
    float m_NoisePhase;
    Vector3 m_CameraPosition;
    float m_TanSunAngularRadius;
    Vector2U m_OutputResolution;
    uint32_t m_bDoDenoising;
};

struct PackNormalAndRoughnessConsts
{
    Vector2U m_OutputResolution;
};

#endif // #define _SHADOW_MASK_STRUCTS_
