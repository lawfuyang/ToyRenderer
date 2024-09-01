#ifndef _POSTPROCESS_CONSTANTS_
#define _POSTPROCESS_CONSTANTS_

#include "StructsCommon.h"

struct PostProcessParameters
{
	Vector2U m_OutputDims;
	float m_ManualExposure;
	float m_MiddleGray;
	float m_WhitePoint;
	float m_BloomStrength;
};

#endif // #define _POSTPROCESS_CONSTANTS_