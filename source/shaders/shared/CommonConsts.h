#ifndef _COMMON_CONSTS_H_
#define _COMMON_CONSTS_H_

#include "ShaderInterop.h"

static const uint32_t kNumThreadsPerWave = 32;
static const uint32_t kMaxThreadGroupsPerDimension = 65535;
static const float kFP16Max = 65504.0f;

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

static const uint32_t kMaxMeshletVertices = 64;
static const uint32_t kMaxMeshletTriangles = 96;
static const uint32_t kMeshletShaderThreadGroupSize = 96;

static const uint32_t kMaxNumMeshLODs = 8;
static const uint32_t kInvalidMeshLOD = 0xFF;

static const uint32_t kDeferredLightingDebugMode_LightingOnly      = 1;
static const uint32_t kDeferredLightingDebugMode_ColorizeInstances = 2;
static const uint32_t kDeferredLightingDebugMode_ColorizeMeshlets  = 3;
static const uint32_t kDeferredLightingDebugMode_Albedo            = 4;
static const uint32_t kDeferredLightingDebugMode_Normal            = 5;
static const uint32_t kDeferredLightingDebugMode_Emissive          = 6;
static const uint32_t kDeferredLightingDebugMode_Metalness         = 7;
static const uint32_t kDeferredLightingDebugMode_Roughness         = 8;
static const uint32_t kDeferredLightingDebugMode_AmbientOcclusion  = 9;
static const uint32_t kDeferredLightingDebugMode_Ambient           = 10;
static const uint32_t kDeferredLightingDebugMode_ShadowMask        = 11;
static const uint32_t kDeferredLightingDebugMode_MeshLOD           = 12;
static const uint32_t kDeferredLightingDebugMode_MotionVectors     = 13;

#endif // #define _COMMON_CONSTS_H_
