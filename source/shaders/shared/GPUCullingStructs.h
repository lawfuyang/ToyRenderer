#ifndef _GPU_CULLING_STRUCTS_H_
#define _GPU_CULLING_STRUCTS_H_

#include "StructsCommon.h"

static const uint32_t kCullingEarlyBufferCounterIdx = 0;
static const uint32_t kCullingLateBufferCounterIdx = 1;
static const uint32_t kNbGPUCullingBufferCounters = 2;

static const uint32_t CullingFlag_FrustumCullingEnable = (1 << 0);
static const uint32_t CullingFlag_OcclusionCullingEnable = (1 << 1);

static const uint32_t kNbGPUCullingGroupThreads = 64;

struct GPUCullingPassConstants
{
	uint32_t m_NbInstances;
	uint32_t m_Flags;
	Vector2U m_HZBDimensions;
	Vector4 m_Frustum;
	Matrix m_ViewMatrix;
	Matrix m_PrevViewMatrix;
	float m_NearPlane;
	float m_P00;
    float m_P11;
};

#endif
