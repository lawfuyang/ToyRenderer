#ifndef _AMBIENT_OCCLUSION_CONSTANTS_
#define _AMBIENT_OCCLUSION_CONSTANTS_

#include "ShaderInterop.h"

struct XeGTAOMainPassConstantBuffer
{
	Matrix m_ViewMatrixNoTranslate;
	uint32_t m_Quality;
};

struct XeGTAODenoiseConstants
{
	uint32_t m_FinalApply;
};

#endif // #define _AMBIENT_OCCLUSION_CONSTANTS_
