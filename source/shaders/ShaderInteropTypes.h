#ifndef SHADER_INTEROP_TYPES_H_
#define SHADER_INTEROP_TYPES_H_

#if defined(__cplusplus)
    #include "MathUtilities.h"
#else
    // .cpp -> .hlsl types

    // NOTE: need to manually unpack because hlsl has no built-in support for 8-bit types
    #define UByte4 uint
    #define UByte4N uint
    #define Byte4 uint
    #define Byte4N uint

    #define Half half
    #define Half2 half2
    #define Half4 half4

    #define Vector2 float2
    #define Vector2U uint2
    #define Vector3 float3
    #define Vector3U uint3
    #define Vector4 float4
    #define Vector4U uint4
    #define Matrix float4x4

    #define Quaternion float4
#endif // #if defined(__cplusplus)

#endif // SHADER_INTEROP_TYPES_H_