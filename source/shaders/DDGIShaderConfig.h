#ifndef RTXGI_DDGI_SHADER_CONFIG_H
#define RTXGI_DDGI_SHADER_CONFIG_H

// DDGI Shader Configuration options
#define RTXGI_DDGI_RESOURCE_MANAGEMENT 0
#define RTXGI_DDGI_BLEND_SHARED_MEMORY 1
#define RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY 1
#define RTXGI_DDGI_BLEND_RAYS_PER_PROBE 256
#define RTXGI_DDGI_WAVE_LANE_COUNT 32

static const uint32_t kNumProbeRadianceTexels = 8;
static const uint32_t kNumProbeDistanceTexels = 16;

#if RTXGI_DDGI_BLEND_RADIANCE || REDUCTION
    #define RTXGI_DDGI_PROBE_NUM_TEXELS kNumProbeRadianceTexels
    #define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS (kNumProbeRadianceTexels - 2)
#else
    #define RTXGI_DDGI_PROBE_NUM_TEXELS kNumProbeDistanceTexels
    #define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS (kNumProbeDistanceTexels - 2)
#endif

// Coordinate System
// 0: RTXGI_COORDINATE_SYSTEM_LEFT
// 1: RTXGI_COORDINATE_SYSTEM_LEFT_Z_UP
// 2: RTXGI_COORDINATE_SYSTEM_RIGHT
// 3: RTXGI_COORDINATE_SYSTEM_RIGHT_Z_UP
#ifndef RTXGI_COORDINATE_SYSTEM
    #define RTXGI_COORDINATE_SYSTEM 0
#endif

// Use Shader Reflection?
// 0: no
// 1: yes
#define RTXGI_DDGI_SHADER_REFLECTION 0

// Should DDGI use bindless resources?
// 0: no
// 1: yes
#define RTXGI_DDGI_BINDLESS_RESOURCES 0

//-------------------------------------------------------------------------------------------------
// Optional Defines (including in this file since we compile with warnings as errors)
// If you change one of these three options in CMake, you need to update them here too!
#define RTXGI_DDGI_DEBUG_PROBE_INDEXING 0
#define RTXGI_DDGI_DEBUG_OCTAHEDRAL_INDEXING 0
#define RTXGI_DDGI_DEBUG_BORDER_COPY_INDEXING 0

// Using the RTXGI SDK's root signature (not bindless)
#define CONSTS_REGISTER b0
#define CONSTS_SPACE space0
#define VOLUME_CONSTS_REGISTER t0
#define VOLUME_CONSTS_SPACE space0
#define RAY_DATA_REGISTER u0
#define RAY_DATA_SPACE space0
#if RTXGI_DDGI_BLEND_RADIANCE
#define OUTPUT_REGISTER u1
#else
#define OUTPUT_REGISTER u2
#endif
#define OUTPUT_SPACE space0
#define PROBE_DATA_REGISTER u3
#define PROBE_DATA_SPACE space0
#define PROBE_VARIABILITY_REGISTER u4
#define PROBE_VARIABILITY_AVERAGE_REGISTER u5
#define PROBE_VARIABILITY_SPACE space0

// cross-reference with the above resource register macros
static const uint32_t kDDGIRootConstsRegister = 0; // CONSTS_REGISTER
static const uint32_t kDDGIVolumeDescGPUPackedRegister = 0; // VOLUME_CONSTS_REGISTER
static const uint32_t kDDGIRayDataRegister = 0; // RAY_DATA_REGISTER
static const uint32_t kDDGIBlendRadianceOutputRegister = 1; // OUTPUT_REGISTER (for Radiance Blend)
static const uint32_t kDDGIBlendDistanceOutputRegister = 2; // OUTPUT_REGISTER (for Distance Blend)
static const uint32_t kDDGIProbeDataRegister = 3; // PROBE_DATA_REGISTER
static const uint32_t kDDGIProbeVariabilityRegister = 4; // PROBE_VARIABILITY_REGISTER
static const uint32_t kDDGIProbeVariabilityAverageRegister = 5; // PROBE_VARIABILITY_AVERAGE_REGISTER

#endif // RTXGI_DDGI_SHADER_CONFIG_H