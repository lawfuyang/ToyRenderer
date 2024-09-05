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

constexpr float ConvertToRadians(float fDegrees) { return DirectX::XMConvertToRadians(fDegrees); }
constexpr float ConvertToDegrees(float fRadians) { return DirectX::XMConvertToDegrees(fRadians); }
inline void ScalarSinCos(float& sinResult, float& cosResult, float value) { return DirectX::XMScalarSinCos(&sinResult, &cosResult, value); }

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
