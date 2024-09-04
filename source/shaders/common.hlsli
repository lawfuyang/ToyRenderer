#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

// Constants from <math.h>
#define M_E                 2.71828182845904523536  // e
#define M_LOG2E             1.44269504088896340736  // log2(e)
#define M_LOG10E            0.434294481903251827651 // log10(e)
#define M_LN2               0.693147180559945309417 // ln(2)
#define M_LN10              2.30258509299404568402  // ln(10)
#define M_PI                3.14159265358979323846  // pi
#define M_PI_2              1.57079632679489661923  // pi/2
#define M_PI_4              0.785398163397448309616 // pi/4
#define M_1_PI              0.318309886183790671538 // 1/pi
#define M_2_PI              0.636619772367581343076 // 2/pi
#define M_2_SQRTPI          1.12837916709551257390  // 2/sqrt(pi)
#define M_SQRT2             1.41421356237309504880  // sqrt(2)
#define M_SQRT1_2           0.707106781186547524401 // 1/sqrt(2)

// Additional constants
#define M_2PI               6.28318530717958647693  // 2pi
#define M_4PI               12.5663706143591729539  // 4pi
#define M_4_PI              1.27323954473516268615  // 4/pi
#define M_1_2PI             0.159154943091895335769 // 1/2pi
#define M_1_4PI             0.079577471545947667884 // 1/4pi
#define M_SQRTPI            1.77245385090551602730  // sqrt(pi)
#define M_1_SQRT2           0.707106781186547524401 // 1/sqrt(2)
#define M_3_16PI            0.05968310365946075     // 3.0 / ( 16.0 * pi )

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
}
/***************************************************************/

// R11G11B10_UNORM <-> float3
float3 R11G11B10_UNORM_to_FLOAT3(uint packedInput)
{
    float3 unpackedOutput;
    unpackedOutput.x = (float)((packedInput) & 0x000007ff) / 2047.0f;
    unpackedOutput.y = (float)((packedInput >> 11) & 0x000007ff) / 2047.0f;
    unpackedOutput.z = (float)((packedInput >> 22) & 0x000003ff) / 1023.0f;
    return unpackedOutput;
}

// 'unpackedInput' is float3 and not float3 on purpose as half float lacks precision for below!
uint FLOAT3_to_R11G11B10_UNORM(float3 unpackedInput)
{
    uint packedOutput;
    packedOutput = ((uint(saturate(unpackedInput.x) * 2047 + 0.5f)) |
        (uint(saturate(unpackedInput.y) * 2047 + 0.5f) << 11) |
        (uint(saturate(unpackedInput.z) * 1023 + 0.5f) << 22));
    return packedOutput;
}

float4 R8G8B8A8_UNORM_to_FLOAT4(uint packedInput)
{
    float4 unpackedOutput;
    unpackedOutput.x = (float)(packedInput & 0x000000ff) / (float)255;
    unpackedOutput.y = (float)(((packedInput >> 8) & 0x000000ff)) / (float)255;
    unpackedOutput.z = (float)(((packedInput >> 16) & 0x000000ff)) / (float)255;
    unpackedOutput.w = (float)(packedInput >> 24) / (float)255;
    return unpackedOutput;
}

uint FLOAT4_to_R8G8B8A8_UNORM(float4 unpackedInput)
{
    return ((uint(saturate(unpackedInput.x) * (float)255 + (float)0.5)) |
        (uint(saturate(unpackedInput.y) * (float)255 + (float)0.5) << 8) |
        (uint(saturate(unpackedInput.z) * (float)255 + (float)0.5) << 16) |
        (uint(saturate(unpackedInput.w) * (float)255 + (float)0.5) << 24));
}

float3 LessThan(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.f : 0.f,
        (f.y < value) ? 1.f : 0.f,
        (f.z < value) ? 1.f : 0.f);
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

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool ProjectSphere(float3 center, float radius, float znear, float P00, float P11, out float4 aabb)
{
    if (center.z < radius + znear)
        return false;

    float2 cx = -center.xz;
    float2 vx = float2(sqrt(dot(cx, cx) - radius * radius), radius);
    float2 minx = mul(cx, float2x2(vx.x, vx.y, -vx.y, vx.x));
    float2 maxx = mul(cx, float2x2(vx.x, -vx.y, vx.y, vx.x));

    float2 cy = -center.yz;
    float2 vy = float2(sqrt(dot(cy, cy) - radius * radius), radius);
    float2 miny = mul(cy, float2x2(vy.x, vy.y, -vy.y, vy.x));
    float2 maxy = mul(cy, float2x2(vy.x, -vy.y, vy.y, vy.x));

    aabb = float4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
    aabb = aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) + float4(0.5f, 0.5f, 0.5f, 0.5f); // clip space -> uv space

    return true;
}

float4 MakeQuaternion(float angle_radian, float3 axis)
{
  // create quaternion using angle and rotation axis
  float4 quaternion;
  float halfAngle = 0.5f * angle_radian;
  float sinHalf = sin(halfAngle);

  quaternion.w = cos(halfAngle);
  quaternion.xyz = sinHalf * axis.xyz;

  return quaternion;
}

float4 InverseQuaternion(float4 q)
{
  float lengthSqr = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;

  if (lengthSqr < 0.001)
  {
    return float4(0, 0, 0, 1.0f);
  }

  q.x = -q.x / lengthSqr;
  q.y = -q.y / lengthSqr;
  q.z = -q.z / lengthSqr;
  q.w =  q.w / lengthSqr;

  return q;
}

float3 MultQuaternionAndVector(float4 q, float3 v)
{
  float3 uv, uuv;
  float3 qvec = float3(q.x, q.y, q.z);
  uv = cross(qvec, v);
  uuv = cross(qvec, uv);
  uv *= (2.0f * q.w);
  uuv *= 2.0f;

  return v + uv + uuv;
}

float4 MultQuaternionAndQuaternion(float4 qA, float4 qB)
{
  float4 q;

  q.w = qA.w * qB.w - qA.x * qB.x - qA.y * qB.y - qA.z * qB.z;
  q.x = qA.w * qB.x + qA.x * qB.w + qA.y * qB.z - qA.z * qB.y;
  q.y = qA.w * qB.y + qA.y * qB.w + qA.z * qB.x - qA.x * qB.z;
  q.z = qA.w * qB.z + qA.z * qB.w + qA.x * qB.y - qA.y * qB.x;

  return q;
}

float4 AngularVelocityToSpin(float4 orientation, float3 angular_veloctiy)
{
  return 0.5f * MultQuaternionAndQuaternion(float4(angular_veloctiy.xyz, 0), orientation);
}

float3 MultWorldInertiaInvAndVector(float4 orientation, float3 inertia, float3 vec)
{
  float4 inv_orientation = float4(-orientation.xyz, orientation.w) / length(orientation);
  float3 inv_inertia = 1.0f / inertia;

  float3 InertiaInv_RotT_vec = inv_inertia * MultQuaternionAndVector(inv_orientation, vec );
  float3 Rot_InertiaInv_RotT_vec = MultQuaternionAndVector(orientation, InertiaInv_RotT_vec );

  return Rot_InertiaInv_RotT_vec;
}

float CubicBezierCurve(float v1, float v2, float v3, float v4, float t)
{
    return (1.0 - t) * (1.0 - t) * (1.0 - t) * v1 + 3.0f * (1.0 - t) * (1.0 - t) * t * v2 + 3.0f * t * t * (1.0 - t) * v3 + t * t * t * v4;
}

template <typename T>
T Min3(T a, T b, T c)
{
    return min(min(a, b), c);
}

template <typename T>
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

uint DivideAndRoundUp(uint x, uint y)
{
    return (x + y - 1) / y;
}

// Octahedron Normal Vectors
// [Cigolle 2014, "A Survey of Efficient Representations for Independent Unit Vectors"]
//						Mean	Max
// oct		8:8			0.33709 0.94424
// snorm	8:8:8		0.17015 0.38588
// oct		10:10		0.08380 0.23467
// snorm	10:10:10	0.04228 0.09598
// oct		12:12		0.02091 0.05874
float2 UnitVectorToOctahedron( float3 N )
{
	N.xy /= dot( 1, abs(N) );
	if( N.z <= 0 )
	{
		N.xy = ( 1 - abs(N.yx) ) * select( N.xy >= 0, float2(1,1), float2(-1,-1) );
	}
	return N.xy;
}

float3 OctahedronToUnitVector( float2 Oct )
{
	float3 N = float3( Oct, 1 - dot( 1, abs(Oct) ) );
	float t = max( -N.z, 0 );
	N.xy += select(N.xy >= 0, float2(-t, -t), float2(t, t));
	return normalize(N);
}

float2 UnitVectorToHemiOctahedron( float3 N )
{
	N.xy /= dot( 1, abs(N) );
	return float2( N.x + N.y, N.x - N.y );
}

float3 HemiOctahedronToUnitVector( float2 Oct )
{
	Oct = float2( Oct.x + Oct.y, Oct.x - Oct.y );
	float3 N = float3( Oct, 2.0 - dot( 1, abs(Oct) ) );
	return normalize(N);
}

float LinearizeDepth(float z, float nearPlane, float farPlane)
{
    return farPlane / (farPlane + z * (nearPlane - farPlane));
}

// https://graphics.stanford.edu/%7Eseander/bithacks.html
uint BitCount(uint value) {
    value = value - ((value >> 1u) & 0x55555555u);
    value = (value & 0x33333333u) + ((value >> 2u) & 0x33333333u);
    return ((value + (value >> 4u) & 0xF0F0F0Fu) * 0x1010101u) >> 24u;
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
