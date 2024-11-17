#pragma once

#include "SimpleMath.h"

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

constexpr float ConvertToRadians(float fDegrees) { return DirectX::XMConvertToRadians(fDegrees); }
constexpr float ConvertToDegrees(float fRadians) { return DirectX::XMConvertToDegrees(fRadians); }
inline void ScalarSinCos(float& sinResult, float& cosResult, float value) { return DirectX::XMScalarSinCos(&sinResult, &cosResult, value); }

constexpr uint32_t GetNextPow2(uint32_t x)
{
    if (x == 0) return 1; // Special case: 0 returns 1 (2^0)

    // Decrement x by 1, and then perform a series of bit shifts to propagate the highest bit.
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    // Now x has all bits set to 1 from the most significant bit down to the least significant bit of the original number.
    return x + 1;
}

/** Divides two integers and rounds up */
constexpr uint32_t DivideAndRoundUp(uint32_t Dividend, uint32_t Divisor)
{
    return (Dividend + Divisor - 1) / Divisor;
}

void ModifyPerspectiveMatrix(Matrix& mat, float nearPlane, float farPlane, bool bReverseZ, bool bInfiniteZ);
Vector2 ProjectWorldPositionToViewport(const Vector3& worldPos, const Matrix& viewProjMatrix, const Vector2U& viewportDim);
