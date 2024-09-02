#include "MathUtilities.h"

void ModifyPerspectiveMatrix(Matrix& mat, float nearPlane, float farPlane, bool bReverseZ, bool bInfiniteZ)
{
    // ReverseZ puts far plane at Z=0 and near plane at Z=1. Redistributes precision more evenly across the entire range
    // It requires clearing Z to 0.0f and using a GREATER variant depth test
    // Some care must also be done to properly reconstruct linear W in a pixel shader from hyperbolic Z

    float Q1, Q2;
    if (bReverseZ)
    {
        if (bInfiniteZ)
        {
            Q1 = 0.0f;
            Q2 = nearPlane;
        }
        else
        {
            Q1 = nearPlane / (farPlane - nearPlane);
            Q2 = Q1 * farPlane;
        }
    }
    else
    {
        if (bInfiniteZ)
        {
            Q1 = -1.0f;
            Q2 = -nearPlane;
        }
        else
        {
            Q1 = farPlane / (nearPlane - farPlane);
            Q2 = Q1 * nearPlane;
        }
    }
    mat._33 = Q1;
    mat._43 = Q2;
}

void GetFrustumCornersWorldSpace(const Matrix& projview, Vector3(&frustumCorners)[8])
{
    const Matrix inv = projview.Invert();

    Vector3* outputPtr = frustumCorners;
    for (uint32_t x = 0; x < 2; ++x)
    {
        for (uint32_t y = 0; y < 2; ++y)
        {
            for (uint32_t z = 0; z < 2; ++z)
            {
                const Vector4 pt = Vector4::Transform(Vector4{ 2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f }, inv);
                *outputPtr++ = Vector3{ pt } / pt.w;
            }
        }
    }
}

AABB MakeLocalToWorldAABB(const AABB& aabb, const Matrix& worldMatrix)
{
    const Vector3 globalCenter = Vector3::Transform(aabb.Center, worldMatrix);

    // Scaled orientation
    const Vector3 right = worldMatrix.Right() * aabb.Extents.x;
    const Vector3 up = worldMatrix.Up() * aabb.Extents.y;
    const Vector3 forward = worldMatrix.Forward() * aabb.Extents.z;

    const float newIi = std::abs(Vector3::UnitX.Dot(right)) + std::abs(Vector3::UnitX.Dot(up)) + std::abs(Vector3::UnitX.Dot(forward));
    const float newIj = std::abs(Vector3::UnitY.Dot(right)) + std::abs(Vector3::UnitY.Dot(up)) + std::abs(Vector3::UnitY.Dot(forward));
    const float newIk = std::abs(Vector3::UnitZ.Dot(right)) + std::abs(Vector3::UnitZ.Dot(up)) + std::abs(Vector3::UnitZ.Dot(forward));

    return AABB{ globalCenter, Vector3{ newIi, newIj, newIk } };
}

Sphere MakeLocalToWorldSphere(const Sphere& sphere, const Matrix& worldMatrix)
{
    const Vector3 globalCenter = Vector3::Transform(sphere.Center, worldMatrix);
    const float globalRadius = std::max(std::max(worldMatrix._11, worldMatrix._22), worldMatrix._33) * sphere.Radius;

    return Sphere{ globalCenter, globalRadius };
}

Vector2 ProjectWorldPositionToViewport(const Vector3& worldPos, const Matrix& viewProjMatrix, const Vector2U& viewportDim)
{
    Vector4 worldPosVec4{ Vector3{ worldPos } };
    worldPosVec4.w = 1.0f;

    Vector4 screenCenter = Vector4::Transform(worldPosVec4, viewProjMatrix);
    screenCenter.x /= screenCenter.w;
    screenCenter.y /= screenCenter.w;

    // ClipXYToUV
    screenCenter.x = screenCenter.x * 0.5f + 0.5f;
    screenCenter.y = screenCenter.y * -0.5f + 0.5f;

    return Vector2{ screenCenter.x * viewportDim.x, screenCenter.y * viewportDim.y };
}
