#pragma once

#include "extern/simplemath/SimpleMath.h"

using Vector2    = DirectX::SimpleMath::Vector2;
using Vector2I   = DirectX::XMINT2;
using Vector2U   = DirectX::XMUINT2;
using Vector3    = DirectX::SimpleMath::Vector3;
using Vector3I   = DirectX::XMINT3;
using Vector3U   = DirectX::XMUINT3;
using Vector4    = DirectX::SimpleMath::Vector4;
using Vector4I   = DirectX::XMINT4;
using Vector4U   = DirectX::XMUINT4;
using Matrix     = DirectX::SimpleMath::Matrix;

using Plane       = DirectX::SimpleMath::Plane;
using Quaternion  = DirectX::SimpleMath::Quaternion;
using Color       = DirectX::SimpleMath::Color;
using Ray         = DirectX::SimpleMath::Ray;
using Viewport    = DirectX::SimpleMath::Viewport;
using Sphere      = DirectX::BoundingSphere;
using AABB        = DirectX::BoundingBox;
using OBB         = DirectX::BoundingOrientedBox;
using Frustum     = DirectX::BoundingFrustum;

#define KINDA_SMALL_NUMBER                   1e-4f
#define KINDA_BIG_NUMBER                     1e10f
#define NullWithEpsilon(a)                   (std::fabsf(a) <= KINDA_SMALL_NUMBER)
#define EqualWithEpsilon(a, b)               (std::fabsf(a - b) <= KINDA_SMALL_NUMBER)
#define GreaterWithEpsilon(a, b)             ((a) - (b) > KINDA_SMALL_NUMBER)
#define GreaterOrEqualWithEpsilon(a, b)      ((b) - (a) < KINDA_SMALL_NUMBER)
#define LesserWithEpsilon(a, b)              ((b) - (a) > KINDA_SMALL_NUMBER)
#define LesserOrEqualWithEpsilon(a, b)       ((a) - (b) < KINDA_SMALL_NUMBER)

inline void ScalarSinCos(float& sinResult, float& cosResult, float value) { return DirectX::XMScalarSinCos(&sinResult, &cosResult, value); }
inline bool NearEqual(const Vector4& v1, const Vector4& v2, const Vector4& epsilon = Vector4{ KINDA_SMALL_NUMBER }) { return DirectX::XMVector4NearEqual(v1, v2, epsilon); };
inline bool NearEqual(const Vector3& v1, const Vector3& v2, const Vector3& epsilon = Vector3{ KINDA_SMALL_NUMBER }) { return DirectX::XMVector3NearEqual(v1, v2, epsilon); };
inline bool NearZero(const Vector4& v) { return NearEqual(v, Vector4::Zero); }
inline bool NearZero(const Vector3& v) { return NearEqual(v, Vector3::Zero); }

#define CREATE_SIMD_FUNCTIONS( TYPE )                                                                                   \
    static TYPE Sqrt( TYPE s )                                   { return TYPE(DirectX::XMVectorSqrt(s)); }             \
    static TYPE Reciprocal( TYPE s )                             { return TYPE(DirectX::XMVectorReciprocal(s)); }       \
    static TYPE ReciprocalSqrt( TYPE s )                         { return TYPE(DirectX::XMVectorReciprocalSqrt(s)); }   \
    static TYPE Floor( TYPE s )                                  { return TYPE(DirectX::XMVectorFloor(s)); }            \
    static TYPE Ceiling( TYPE s )                                { return TYPE(DirectX::XMVectorCeiling(s)); }          \
    static TYPE Round( TYPE s )                                  { return TYPE(DirectX::XMVectorRound(s)); }            \
    static TYPE Exp( TYPE s )                                    { return TYPE(DirectX::XMVectorExp(s)); }              \
    static TYPE Pow( TYPE b, TYPE e )                            { return TYPE(DirectX::XMVectorPow(b, e)); }           \
    static TYPE Log( TYPE s )                                    { return TYPE(DirectX::XMVectorLog(s)); }              \
    static TYPE Sin( TYPE s )                                    { return TYPE(DirectX::XMVectorSin(s)); }              \
    static TYPE Cos( TYPE s )                                    { return TYPE(DirectX::XMVectorCos(s)); }              \
    static TYPE Tan( TYPE s )                                    { return TYPE(DirectX::XMVectorTan(s)); }              \
    static TYPE ASin( TYPE s )                                   { return TYPE(DirectX::XMVectorASin(s)); }             \
    static TYPE ACos( TYPE s )                                   { return TYPE(DirectX::XMVectorACos(s)); }             \
    static TYPE ATan( TYPE s )                                   { return TYPE(DirectX::XMVectorATan(s)); }             \
    static TYPE ATan2( TYPE y, TYPE x )                          { return TYPE(DirectX::XMVectorATan2(y, x)); }         \
    static TYPE Lerp( TYPE a, TYPE b, TYPE t )                   { return TYPE(DirectX::XMVectorLerpV(a, b, t)); }      \
    static TYPE MultiplyAdd( TYPE v1, TYPE v2, TYPE v3 )         { return DirectX::XMVectorMultiplyAdd(v1, v2, v3); }   \
    static TYPE VectorLess( TYPE lhs, TYPE rhs )                 { return DirectX::XMVectorLess(lhs, rhs); }            \
    static TYPE VectorLessEqual( TYPE lhs, TYPE rhs )            { return DirectX::XMVectorLessOrEqual(lhs, rhs); }     \
    static TYPE VectorGreater( TYPE lhs, TYPE rhs )              { return DirectX::XMVectorGreater(lhs, rhs); }         \
    static TYPE VectorGreaterOrEqual( TYPE lhs, TYPE rhs )       { return DirectX::XMVectorGreaterOrEqual(lhs, rhs); }  \
    static TYPE VectorEqual( TYPE lhs, TYPE rhs )                { return DirectX::XMVectorEqual(lhs, rhs); }           \
    static TYPE VectorSelect( TYPE lhs, TYPE rhs, TYPE control ) { return DirectX::XMVectorSelect(lhs, rhs, control); } \

CREATE_SIMD_FUNCTIONS(Vector3)
CREATE_SIMD_FUNCTIONS(Vector4)

inline Vector4 Normalize3(const Vector4& v4)
{
    Vector3 v3{ v4.x, v4.y, v4.z };
    v3.Normalize();
    return Vector4{ v3.x, v3.y, v3.z, 1.0f };
}

template <typename T>
inline constexpr T Saturate(T v)
{
    return std::clamp(v, static_cast<T>(0), static_cast<T>(1));
}

//damps a value using a deltatime and a speed
template <typename T> inline T Damp(T val, T target, T speed, float dt)
{
    T maxDelta = speed * dt;
    return val + std::clamp(target - val, -maxDelta, maxDelta);
}

template <typename T> T SmoothStep(T min, T max, T f)
{
    f = std::clamp((f - min) / (max - min), T(0.f), T(1.f));
    return f * f * (T(3.f) - T(2.f) * f);
}

template <typename T> T SmootherStep(T min, T max, T f)
{
    f = std::clamp((f - min) / (max - min), T(0.f), T(1.f));
    return f * f * f * (f * (f * T(6.f) - T(15.f)) + T(10.f));
}

template <typename T> constexpr bool IsAligned(T value, size_t alignment)
{
    return ((size_t)value & (alignment - 1)) == 0;
}

template <typename T> constexpr T AlignUpWithMask(T value, size_t mask)
{
    return (T)(((size_t)value + mask) & ~mask);
}

template <typename T> constexpr T AlignDownWithMask(T value, size_t mask)
{
    return (T)((size_t)value & ~mask);
}

template <typename T> constexpr T AlignUp(T value, size_t alignment)
{
    return AlignUpWithMask(value, alignment - 1);
}

template <typename T> constexpr T AlignDown(T value, size_t alignment)
{
    return AlignDownWithMask(value, alignment - 1);
}

template <typename T> constexpr T DivideByMultiple(T value, size_t alignment)
{
    return (T)((value + alignment - 1) / alignment);
}

template <typename T> constexpr bool IsPowerOfTwo(T value)
{
    return 0 == (value & (value - 1));
}

template <typename T> constexpr bool IsDivisible(T value, T divisor)
{
    return (value / divisor) * divisor == value;
}

template <typename T> constexpr T AlignPowerOfTwo(T value)
{
    return value == 0 ? 0 : 1 << Log2(value);
}

// round given value to next multiple, not limited to pow2 multiples...
constexpr uint32_t RoundToNextMultiple(uint32_t val, uint32_t multiple)
{
    uint32_t t = val + multiple - 1;
    return t - (t % multiple);
}

constexpr uint32_t GetNextPow2(uint32_t n)
{
    --n;
    n = n | (n >> 1);
    n = n | (n >> 2);
    n = n | (n >> 4);
    n = n | (n >> 8);
    n = n | (n >> 16);
    ++n;

    return n;
}

/** Divides two integers and rounds up */
constexpr uint32_t DivideAndRoundUp(uint32_t Dividend, uint32_t Divisor)
{
    return (Dividend + Divisor - 1) / Divisor;
}

void ModifyPerspectiveMatrix(Matrix& mat, float nearPlane, float farPlane, bool bReverseZ, bool bInfiniteZ);
void GetFrustumCornersWorldSpace(const Matrix& projview, Vector3(&frustumCorners)[8]);
AABB MakeLocalToWorldAABB(const AABB& aabb, const Matrix& worldMatrix);
Sphere MakeLocalToWorldSphere(const Sphere& sphere, const Matrix& worldMatrix);
Vector2 ProjectWorldPositionToViewport(const Vector3& worldPos, const Matrix& viewProjMatrix, const Vector2U& viewportDim);

constexpr float ConvertToRadians(float fDegrees) { return fDegrees * (std::numbers::pi / 180.0f); }
constexpr float ConvertToDegrees(float fRadians) { return fRadians * (180.0f / std::numbers::pi); }
