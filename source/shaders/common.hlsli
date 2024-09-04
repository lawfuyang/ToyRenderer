#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

static const float M_PI = 3.14159265358979323846f;

static const float kNearDepth = 1.0f;
static const float kFarDepth = 0.0f;

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

/***************************************************************/
// Normal encoding
// Ref: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}

// Converts a 3D unit vector to a 2D vector with <0,1> range. 
float2 EncodeNormal(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 DecodeNormal(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);

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

// Christian Schuler, "Normal Mapping without Precomputed Tangents", ShaderX 5, Chapter 2.6, pp. 131-140
// See also follow-up blog post: http://www.thetenthplanet.de/archives/1180
float3x3 CalculateTBN(float3 p, float3 n, float2 tex)
{
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(tex);
    float2 duv2 = ddy(tex);

    float3x3 M = float3x3(dp1, dp2, cross(dp1, dp2));
    float2x3 inverseM = float2x3(cross(M[1], M[2]), cross(M[2], M[0]));
    float3 t = normalize(mul(float2(duv1.x, duv2.x), inverseM));
    float3 b = normalize(mul(float2(duv1.y, duv2.y), inverseM));
    return float3x3(t, b, n);
}

float3 PeturbNormal(float3 localNormal, float3 position, float3 normal, float2 texCoord)
{
    const float3x3 TBN = CalculateTBN(position, normal, texCoord);
    return normalize(mul(localNormal, TBN));
}

float3 TwoChannelNormalX2(float2 normal)
{
    float2 xy = 2.0f * normal - 1.0f;
    float z = sqrt(1 - dot(xy, xy));
    return float3(xy.x, xy.y, z);
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

// same as in MathUtilites.h
void MakeLocalToWorldAABB(float3 aabbCenter, float3 aabbExtents, float4x4 worldMatrix, out float3 outCenter, out float3 outExtents)
{
    float3 globalCenter = mul(float4(aabbCenter, 1), worldMatrix).xyz;

    // Scaled orientation
    float3 right = worldMatrix[0].xyz * aabbExtents.x;
    float3 up = worldMatrix[1].xyz * aabbExtents.y;
    float3 forward = worldMatrix[2].xyz * aabbExtents.z;

    float newIi = abs(dot(float3(1, 0, 0), right)) + abs(dot(float3(1, 0, 0), up)) + abs(dot(float3(1, 0, 0), forward));
    float newIj = abs(dot(float3(0, 1, 0), right)) + abs(dot(float3(0, 1, 0), up)) + abs(dot(float3(0, 1, 0), forward));
    float newIk = abs(dot(float3(0, 0, 1), right)) + abs(dot(float3(0, 0, 1), up)) + abs(dot(float3(0, 0, 1), forward));

    outCenter = globalCenter;
    outExtents = float3(newIi, newIj, newIk);
}

#endif // __COMMON_HLSL__
