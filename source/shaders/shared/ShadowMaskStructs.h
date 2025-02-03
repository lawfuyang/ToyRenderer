#include "ShaderInterop.h"

struct ShadowMaskConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_DirectionalLightDirection;
    float m_NoisePhase;
    Vector2U m_OutputResolution;
    float m_SunSize;
};
