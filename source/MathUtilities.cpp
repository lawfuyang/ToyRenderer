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
