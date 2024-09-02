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

namespace Bezier
{
    Vector3 CubicInterpolate(const Vector3& p1, const Vector3& p2, const Vector3& p3, const Vector3& p4, float t)
    {
        const Vector3 T0{ (1 - t) * (1 - t) * (1 - t) };
        const Vector3 T1{ 3 * t * (1 - t) * (1 - t) };
        const Vector3 T2{ 3 * t * t * (1 - t) };
        const Vector3 T3{ t * t * t };

        Vector3 Result = p1 * T0;
        Result = DirectX::XMVectorMultiplyAdd(p2, T1, Result);
        Result = DirectX::XMVectorMultiplyAdd(p3, T2, Result);
        Result = DirectX::XMVectorMultiplyAdd(p4, T3, Result);

        return Result;
    }

    Vector3 CubicTangent(const Vector3& p1, const Vector3& p2, const Vector3& p3, const Vector3& p4, float t)
    {
        const Vector3 T0{ -1 + 2 * t - t * t };
        const Vector3 T1{ 1 - 4 * t + 3 * t * t };
        const Vector3 T2{ 2 * t - 3 * t * t };
        const Vector3 T3{ t * t };

        Vector3 Result = p1 * T0;
        Result = DirectX::XMVectorMultiplyAdd(p2, T1, Result);
        Result = DirectX::XMVectorMultiplyAdd(p3, T2, Result);
        Result = DirectX::XMVectorMultiplyAdd(p4, T3, Result);

        return Result;
    }

    void CreatePatchVertices(const std::array<Vector3, 16>& patch, uint32_t tessellation, bool isMirrored, PatchVertexOutputFn outputVertexFunc)
    {
        for (uint32_t i = 0; i <= tessellation; i++)
        {
            const float u = float(i) / float(tessellation);

            for (uint32_t j = 0; j <= tessellation; j++)
            {
                const float v = float(j) / float(tessellation);

                // Perform four horizontal bezier interpolations
                // between the control points of this patch.
                const Vector3 p1 = CubicInterpolate(patch[0], patch[1], patch[2], patch[3], u);
                const Vector3 p2 = CubicInterpolate(patch[4], patch[5], patch[6], patch[7], u);
                const Vector3 p3 = CubicInterpolate(patch[8], patch[9], patch[10], patch[11], u);
                const Vector3 p4 = CubicInterpolate(patch[12], patch[13], patch[14], patch[15], u);

                // Perform a vertical interpolation between the results of the
                // previous horizontal interpolations, to compute the position.
                const Vector3 position = CubicInterpolate(p1, p2, p3, p4, v);

                // Perform another four bezier interpolations between the control
                // points, but this time vertically rather than horizontally.
                const Vector3 q1 = CubicInterpolate(patch[0], patch[4], patch[8], patch[12], v);
                const Vector3 q2 = CubicInterpolate(patch[1], patch[5], patch[9], patch[13], v);
                const Vector3 q3 = CubicInterpolate(patch[2], patch[6], patch[10], patch[14], v);
                const Vector3 q4 = CubicInterpolate(patch[3], patch[7], patch[11], patch[15], v);

                // Compute vertical and horizontal tangent vectors.
                const Vector3 tangent1 = CubicTangent(p1, p2, p3, p4, v);
                const Vector3 tangent2 = CubicTangent(q1, q2, q3, q4, u);

                // Cross the two tangent vectors to compute the normal.
                Vector3 normal = tangent1.Cross(tangent2);

                if (!NearZero(normal))
                {
                    normal.Normalize();

                    // If this patch is mirrored, we must invert the normal.
                    if (isMirrored)
                    {
                        normal = -normal;
                    }
                }

                // Compute the texture coordinate.
                const float mirroredU = isMirrored ? 1 - u : u;

                const Vector2 textureCoordinate{ mirroredU, v };

                // Output this vertex.
                outputVertexFunc(position, normal, textureCoordinate);
            }
        }
    }

    void CreatePatchIndices(uint32_t tessellation, bool isMirrored, PatchIndexOutputFn outputIndexFunc)
    {
        const uint32_t stride = tessellation + 1;

        for (uint32_t i = 0; i < tessellation; i++)
        {
            for (uint32_t j = 0; j < tessellation; j++)
            {
                // Make a list of six index values (two triangles).
                std::array<uint32_t, 6> indices =
                {
                    i * stride + j,
                    (i + 1) * stride + j,
                    (i + 1) * stride + j + 1,

                    i * stride + j,
                    (i + 1) * stride + j + 1,
                    i * stride + j + 1,
                };

                // If this patch is mirrored, reverse indices to fix the winding order.
                if (isMirrored)
                {
                    std::reverse(indices.begin(), indices.end());
                }

                // Output these index values.
                std::for_each(indices.begin(), indices.end(), outputIndexFunc);
            }
        }
    }
}
