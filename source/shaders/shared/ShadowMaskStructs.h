#include "ShaderInterop.h"

struct ShadowMaskConsts
{
	Matrix m_InvViewProjMatrix;
    Vector3 m_CameraOrigin;
    float m_InvShadowMapResolution;
    Vector4 m_CSMDistances; // for some reason, this can't be an array of 4 floats. as it will have 16 byte offsets per element?!?!?!?
    Matrix m_DirLightViewProj[4];
    uint32_t m_InversedDepth;
};

struct HardwareRaytraceConsts
{
    Matrix m_InvViewProjMatrix;
    Vector3 m_DirectionalLightDirection;
    uint32_t PAD0;
    Vector2U m_OutputResolution;

};
