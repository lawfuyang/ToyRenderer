#include "ShaderInterop.h"

struct HardwareRaytraceConsts
{
    Matrix m_ClipToWorld;
    Vector3 m_DirectionalLightDirection;
    uint32_t PAD0;
    Vector2U m_OutputResolution;
};
