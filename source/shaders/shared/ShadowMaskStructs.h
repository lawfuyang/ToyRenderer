#include "ShaderInterop.h"

struct ShadowMaskConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_DirectionalLightDirection;
    uint32_t PAD0;
    Vector2U m_OutputResolution;
};
