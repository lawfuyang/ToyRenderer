#ifndef _GPU_CULLING_STRUCTS_H_
#define _GPU_CULLING_STRUCTS_H_

#include "StructsCommon.h"

static const uint32_t kFrustumCullingBufferCounterIdx = 0;
static const uint32_t kOcclusionCullingPhase1BufferCounterIdx = 1;
static const uint32_t kOcclusionCullingPhase2BufferCounterIdx = 2;
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

	Matrix m_WorldToClip;
	Matrix m_PrevFrameWorldToClip;

	Vector2U m_HZBDimensions;
	uint32_t m_HZBMipCount;
};

#endif
