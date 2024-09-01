#ifndef _SKY_CONSTANTS_
#define _SKY_CONSTANTS_

#include "StructsCommon.h"

struct HosekWilkieSkyParameters
{
    Vector4 m_Params[10];
};

struct SkyPassParameters
{
    Matrix m_InvViewProjMatrix;
    Vector3 m_SunLightDir;
    uint32_t PAD0;
    Vector3 m_CameraPosition;
    uint32_t PAD1;
    HosekWilkieSkyParameters m_HosekParams;
};

#endif // #define _SKY_CONSTANTS_