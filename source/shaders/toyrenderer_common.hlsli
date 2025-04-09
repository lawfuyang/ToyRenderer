#pragma once

static const float M_PI = 3.14159265358979323846f;
static const float kNearDepth = 1.0f;
static const float kFarDepth = 0.0f;
static const float kKindaSmallNumber = 1e-4f;
static const float kKindaBigNumber = 1e10f;

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

float GetMaxScaleFromWorldMatrix(float4x4 worldMatrix)
{
    float dx = dot(worldMatrix._11_12_13, worldMatrix._11_12_13); // Length of the X-axis vector
    float dy = dot(worldMatrix._21_22_23, worldMatrix._21_22_23); // Length of the Y-axis vector
    float dz = dot(worldMatrix._31_32_33, worldMatrix._31_32_33); // Length of the Z-axis vector
    return sqrt(Max3(dx, dy, dz));
}

float4x4 MakeTranslationMatrix(float3 translation)
{
    return float4x4(1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    translation.x, translation.y, translation.z, 1.0f);
}

// referred from DirectXMath's XMMatrixRotationQuaternion
float4x4 MatrixFromQuaternion(float4 quaternion)
{
    float qx = quaternion.x;
    float qxx = qx * qx;

    float qy = quaternion.y;
    float qyy = qy * qy;

    float qz = quaternion.z;
    float qzz = qz * qz;

    float qw = quaternion.w;
    
    float4x4 M;
    M[0][0] = 1.f - 2.f * qyy - 2.f * qzz;
    M[0][1] = 2.f * qx * qy + 2.f * qz * qw;
    M[0][2] = 2.f * qx * qz - 2.f * qy * qw;
    M[0][3] = 0.f;

    M[1][0] = 2.f * qx * qy - 2.f * qz * qw;
    M[1][1] = 1.f - 2.f * qxx - 2.f * qzz;
    M[1][2] = 2.f * qy * qz + 2.f * qx * qw;
    M[1][3] = 0.f;

    M[2][0] = 2.f * qx * qz + 2.f * qy * qw;
    M[2][1] = 2.f * qy * qz - 2.f * qx * qw;
    M[2][2] = 1.f - 2.f * qxx - 2.f * qyy;
    M[2][3] = 0.f;
    
    M[3][0] = 0.f;
    M[3][1] = 0.f;
    M[3][2] = 0.f;
    M[3][3] = 1.f;
    
    return M;
}

float4x4 MakeScaleMatrix(float3 scale)
{
    return float4x4(scale.x, 0.0f, 0.0f, 0.0f,
                    0.0f, scale.y, 0.0f, 0.0f,
                    0.0f, 0.0f, scale.z, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
}

float4x4 MakeWorldMatrix(float3 position, float4 rotation, float3 scale)
{
    float4x4 translationMatrix = MakeTranslationMatrix(position);
    float4x4 rotationMatrix = MatrixFromQuaternion(rotation);
    float4x4 scaleMatrix = MakeScaleMatrix(scale);
    
    return mul(mul(rotationMatrix, scaleMatrix), translationMatrix);
}

float4 TransformBoundingSphereToWorld(float4x4 worldMatrix, float4 localSphere)
{
    // Extract the local center (xyz) and radius (w)
    float3 localCenter = localSphere.xyz;
    float localRadius = localSphere.w;

    // Transform the center to world space (position, so w = 1.0)
    float4 worldCenterHomogeneous = mul(float4(localCenter, 1.0), worldMatrix);
    float3 worldCenter = worldCenterHomogeneous.xyz;

    // Calculate the scaling factor from the world matrix
    float maxScale = GetMaxScaleFromWorldMatrix(worldMatrix);

    // Scale the radius by the maximum scaling factor
    float worldRadius = localRadius * maxScale;

    // Return the transformed sphere (center in world space, scaled radius)
    return float4(worldCenter, worldRadius);
}

// Unpacks a 2 channel normal to xyz
float3 TwoChannelNormalX2(float2 normal)
{
    float2 xy = 2.0f * normal - 1.0f;
    float z = sqrt(1 - dot(xy, xy));
    return float3(xy.x, xy.y, z);
}

// Christian Schuler, "Normal Mapping without Precomputed Tangents", ShaderX 5, Chapter 2.6, pp. 131-140
// See also follow-up blog post: http://www.thetenthplanet.de/archives/1180
float3x3 CalculateTBNWithoutTangent(float3 p, float3 n, float2 tex)
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
