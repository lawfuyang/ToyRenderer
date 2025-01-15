#ifndef _HDR_CONSTANTS_
#define _HDR_CONSTANTS_

#include "ShaderInterop.h"

struct GenerateLuminanceHistogramParameters
{
    Vector2U m_SrcColorDims;
    float m_MinLogLuminance;
    float m_InverseLogLuminanceRange;
};

struct AdaptExposureParameters
{
    float m_MinLogLuminance;
    float m_LogLuminanceRange;
    float m_AdaptationSpeed;
    uint32_t m_NbPixels;
};

#endif // #define _HDR_CONSTANTS_
