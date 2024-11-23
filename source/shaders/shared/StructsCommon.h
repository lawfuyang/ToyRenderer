#ifndef _STRUCTS_COMMON_H_
#define _STRUCTS_COMMON_H_

#if defined(__cplusplus)
    #include "MathUtilities.h"
#else
    // .cpp -> .hlsl types
    #define int32_t int
    #define uint32_t uint
    #define Half2 half2
    #define Half4 half4
    #define Vector2 float2
    #define Vector2U uint2
    #define Vector3 float3
    #define Vector3U uint3
    #define Vector4 float4
    #define Vector4U uint4
    #define Matrix float4x4
#endif // #if defined(__cplusplus)

#endif // #define _STRUCTS_COMMON_H_
