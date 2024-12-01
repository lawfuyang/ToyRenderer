#ifndef _GPU_CULLING_STRUCTS_H_
#define _GPU_CULLING_STRUCTS_H_

#include "StructsCommon.h"

static const uint32_t kFrustumCullingBufferCounterIdx = 0;
static const uint32_t kOcclusionCullingEarlyBufferCounterIdx = 1;
static const uint32_t kOcclusionCullingLateBufferCounterIdx = 2;
static const uint32_t kNbGPUCullingBufferCounters = 3;

static const uint32_t OcclusionCullingFlag_Enable = (1 << 0);
static const uint32_t OcclusionCullingFlag_IsFirstPhase = (1 << 1);

static const uint32_t kNbGPUCullingGroupThreads = 64;

struct GPUCullingPassConstants
{
	uint32_t m_NbInstances;
	uint32_t m_EnableFrustumCulling;
	uint32_t m_OcclusionCullingFlags;
	uint32_t m_EnableMeshletConeCulling;

    Matrix m_WorldToClipInclusive; // Culling with this matrix includes the entire primitive
    Matrix m_WorldToClipExclusive; // Culling with this matrix excludes the entire primitive (make _11 to 1.0f to ignore)

	Vector2U m_HZBDimensions;
	uint32_t m_HZBMipCount;
};

#endif
