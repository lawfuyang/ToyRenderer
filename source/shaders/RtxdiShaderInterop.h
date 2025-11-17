#ifndef RTXDI_SHADER_INTEROP_H
#define RTXDI_SHADER_INTEROP_H

#include "ShaderInteropTypes.h"

// simple directional light only for now
// TODO: pack & compress
struct ReSTIRLightInfo
{
    Vector3 m_Direction;
    Vector3 m_Radiance;
    // TODO: add solid angle for dir light
};

struct ReSTIRLightingConstants
{
    RTXDI_LightBufferParameters m_LightBufferParams;
    RTXDI_ReservoirBufferParameters m_ReservoirBufferParams;
    Matrix m_ClipToWorld;
    Vector3 m_CameraPosition;
    uint32_t m_InputBufferIndex;
    Vector2 m_OutputResolutionInv;
    uint32_t m_OutputBufferIndex;
};

#endif // RTXDI_SHADER_INTEROP_H
