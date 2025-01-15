#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

#include "shared/CommonConsts.h"

static const float M_PI = 3.14159265358979323846f;

static const float kNearDepth = 1.0f;
static const float kFarDepth = 0.0f;

float3x3 ToFloat3x3(float4x4 m)
{
    return float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
}

template<typename T>
T Pow4(T x)
{
    T xx = x * x;
    return xx * xx;
}
template<typename T>
T Pow5(T x)
{
    T xx = x * x;
    return xx * xx * x;
}

template<typename T>
T Min3(T a, T b, T c)
{
    return min(min(a, b), c);
}

template<typename T>
T Max3(T a, T b, T c)
{
    return max(max(a, b), c);
}

template<typename T>
T Min4(T a, T b, T c, T d)
{
    return min(min(a, b), min(c, d));
}

template<typename T>
T Max4(T a, T b, T c, T d)
{
    return max(max(a, b), max(c, d));
}

// Convert a UV to clip space coordinates (XY: [-1, 1])
float2 UVToClipXY(float2 uv)
{
    return uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
}

// Convert a clip position after projection and perspective divide to a UV
float2 ClipXYToUV(float2 xy)
{
    return xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
}

float3 ScreenUVToWorldPosition(float2 texcoord, float depth, float4x4 inverseViewProj)
{
    float4 homogenousPosition = mul(float4(UVToClipXY(texcoord), depth, 1.0f), inverseViewProj);
    return homogenousPosition.xyz / homogenousPosition.w;
}

float3 ScreenUVToViewPosition(float2 texcoord, float depth, float4x4 inverseView)
{
    float4 homogenousPosition = mul(float4(UVToClipXY(texcoord), depth, 1.0f), inverseView);
    return homogenousPosition.xyz / homogenousPosition.w;
}

float3 ClipXYToWorldPosition(float2 xy, float depth, float4x4 inverseViewProj)
{
    float4 homogenousPosition = mul(float4(xy, depth, 1.0f), inverseViewProj);
    return homogenousPosition.xyz / homogenousPosition.w;
}

float3 ClipXYToViewPosition(float2 xy, float depth, float4x4 inverseView)
{
    float4 homogenousPosition = mul(float4(xy, depth, 1.0f), inverseView);
    return homogenousPosition.xyz / homogenousPosition.w;
}

// This assumes the default color gamut found in sRGB and REC709. The color primaries determine these coefficients. Note that this operates on linear values, not gamma space.
float RGBToLuminance(float3 x)
{
    return dot(x, float3(0.212671, 0.715160, 0.072169)); // Defined by sRGB/Rec.709 gamut
}

float3 LinearToSRGB(float3 linearRGB)
{
    return pow(linearRGB, 1.0f / 2.2f);
}

uint DivideAndRoundUp(uint x, uint y)
{
    return (x + y - 1) / y;
}

float3x3 MakeAdjugateMatrix(float4x4 m)
{
    return float3x3
    (
		cross(m[1].xyz, m[2].xyz),
		cross(m[2].xyz, m[0].xyz),
		cross(m[0].xyz, m[1].xyz)
	);
}

// Niagara's frustum culling
bool FrustumCull(float3 sphereCenterViewSpace, float radius, float4 frustum)
{
    bool visible = true;
    
	// the left/top/right/bottom plane culling utilizes frustum symmetry to cull against two planes at the same time
    visible &= sphereCenterViewSpace.z * frustum.y + abs(sphereCenterViewSpace.x) * frustum.x < radius;
    visible &= sphereCenterViewSpace.z * frustum.w + abs(sphereCenterViewSpace.y) * frustum.z < radius;
    
	// the near plane culling uses camera space Z directly
    // NOTE: this seems unnecessary?
#if 0
    visible &= (sphereCenterViewSpace.z - radius) < g_GPUCullingPassConstants.m_NearPlane;
#endif
    
    return visible;
}

bool ConeCull(float3 sphereCenterViewSpace, float radius, float3 coneAxis, float coneCutoff)
{
    return dot(sphereCenterViewSpace, coneAxis) >= coneCutoff * length(sphereCenterViewSpace) + radius;
}

#endif // __COMMON_HLSL__
