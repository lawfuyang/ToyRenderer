#ifndef _COMMON_CONSTS_H_
#define _COMMON_CONSTS_H_

#include "ShaderInterop.h"

static const uint32_t kNumThreadsPerWave = 32;

static const uint32_t MaterialFlag_UseDiffuseTexture           = (1 << 0);
static const uint32_t MaterialFlag_UseNormalTexture            = (1 << 1);
static const uint32_t MaterialFlag_UseMetallicRoughnessTexture = (1 << 2);
static const uint32_t MaterialFlag_UseEmissiveTexture          = (1 << 3);

static const uint32_t SamplerIdx_AnisotropicClamp  = 0;
static const uint32_t SamplerIdx_AnisotropicWrap   = 1;
static const uint32_t SamplerIdx_AnisotropicBorder = 2;
static const uint32_t SamplerIdx_AnisotropicMirror = 3;
static const uint32_t SamplerIdx_Count             = 4;

static const uint32_t kCullingEarlyInstancesBufferCounterIdx = 0;
static const uint32_t kCullingEarlyMeshletsBufferCounterIdx = 1;
static const uint32_t kCullingLateInstancesBufferCounterIdx = 2;
static const uint32_t kCullingLateMeshletsBufferCounterIdx = 3;
static const uint32_t kNbGPUCullingBufferCounters = 4;

static const uint32_t kCullingFlagFrustumCullingEnable     = (1 << 0);
static const uint32_t kCullingFlagOcclusionCullingEnable   = (1 << 1);
static const uint32_t kCullingFlagMeshletConeCullingEnable = (1 << 2);

#endif // #define _COMMON_CONSTS_H_
