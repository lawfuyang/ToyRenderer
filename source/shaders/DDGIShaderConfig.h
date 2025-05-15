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
#define RTXGI_COORDINATE_SYSTEM 0

// Use Shader Reflection?
// 0: no
// 1: yes
#define RTXGI_DDGI_SHADER_REFLECTION 0

// Bindless Resource implementation type
// 0: RTXGI_BINDLESS_TYPE_RESOURCE_ARRAYS
// 1: RTXGI_BINDLESS_TYPE_DESCRIPTOR_HEAP
#define RTXGI_BINDLESS_TYPE 1

// Should DDGI use bindless resources?
// 0: no
// 1: yes
#define RTXGI_DDGI_BINDLESS_RESOURCES 1

//-------------------------------------------------------------------------------------------------
// Optional Defines (including in this file since we compile with warnings as errors)
// If you change one of these three options in CMake, you need to update them here too!
#define RTXGI_DDGI_DEBUG_PROBE_INDEXING 0
#define RTXGI_DDGI_DEBUG_OCTAHEDRAL_INDEXING 0
#define RTXGI_DDGI_DEBUG_BORDER_COPY_INDEXING 0

// Shader resource registers and spaces
#define CONSTS_REGISTER b0
#define CONSTS_SPACE space0

// cross-reference with the above resource register macros
static const uint32_t kDDGIRootConstsRegister = 0; // CONSTS_REGISTER

// NOTE: these registers are not used when RTXGI_BINDLESS_TYPE_DESCRIPTOR_HEAP is used
// but we still need to define them, so set everything to '0', else the Shaders cant compile due to bad implementation of *Defines.hlsl
#define VOLUME_CONSTS_REGISTER t0
#define VOLUME_CONSTS_SPACE space0
#define RAY_DATA_REGISTER u0
#define RAY_DATA_SPACE space0
#define OUTPUT_REGISTER u0
#define OUTPUT_SPACE space0
#define PROBE_DATA_REGISTER u0
#define PROBE_DATA_SPACE space0
#define PROBE_VARIABILITY_REGISTER u0
#define PROBE_VARIABILITY_AVERAGE_REGISTER u0
#define PROBE_VARIABILITY_SPACE space0

#endif // RTXGI_DDGI_SHADER_CONFIG_H
