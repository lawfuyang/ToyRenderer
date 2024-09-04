#ifndef _HZB_H_
#define _HZB_H_

#include "common.hlsli"

float4x4 Get4x4HZBKernel(float4 rectMinMaxUV, Texture2D<float> HZBTexture, float2 hzbDimensions, SamplerState pointClampSampler)
{
    int4 rectMinMaxTexel = rectMinMaxUV * hzbDimensions.xyxy;

    // Clamp bounds
    rectMinMaxTexel.xy = max(rectMinMaxTexel.xy, 0);
    rectMinMaxTexel.zw = min(rectMinMaxTexel.zw, hzbDimensions.xy);

    // Compute the mip level. * 0.5 as we have a 4x4 pixel sample kernel
    float2 rectSize = (rectMinMaxTexel.zw - rectMinMaxTexel.xy) * 0.5f;
    int mip = max(ceil(log2(max(rectSize.x, rectSize.y))), 0);

    // Determine whether a higher res mip can be used
    int levelLower = max(mip - 1, 0);
    float4 lowerRect = rectMinMaxTexel * exp2(-levelLower);
    float2 lowerRectSize = ceil(lowerRect.zw) - floor(lowerRect.xy);
    if (lowerRectSize.x <= 4 && lowerRectSize.y <= 4)
        mip = levelLower;

    // Transform the texel coordinates for the selected mip
    rectMinMaxTexel >>= mip;
    float2 texelSize = rcp(hzbDimensions) * (1u << mip);

    float4 xCoords = (min(rectMinMaxTexel.x + float4(0, 1, 2, 3), rectMinMaxTexel.z) + 0.5f) * texelSize.x;
    float4 yCoords = (min(rectMinMaxTexel.y + float4(0, 1, 2, 3), rectMinMaxTexel.w) + 0.5f) * texelSize.y;

    float depth00 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.x, yCoords.x), mip);
    float depth10 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.y, yCoords.x), mip);
    float depth20 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.z, yCoords.x), mip);
    float depth30 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.w, yCoords.x), mip);

    float depth01 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.x, yCoords.y), mip);
    float depth11 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.y, yCoords.y), mip);
    float depth21 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.z, yCoords.y), mip);
    float depth31 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.w, yCoords.y), mip);

    float depth02 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.x, yCoords.z), mip);
    float depth12 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.y, yCoords.z), mip);
    float depth22 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.z, yCoords.z), mip);
    float depth32 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.w, yCoords.z), mip);

    float depth03 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.x, yCoords.w), mip);
    float depth13 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.y, yCoords.w), mip);
    float depth23 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.z, yCoords.w), mip);
    float depth33 = HZBTexture.SampleLevel(pointClampSampler, float2(xCoords.w, yCoords.w), mip);

    return float4x4(
        float4(depth00, depth01, depth02, depth03),
        float4(depth10, depth11, depth12, depth13),
        float4(depth20, depth21, depth22, depth23),
        float4(depth30, depth31, depth32, depth33)
    );
}

// Returns the minimum depth value from the HZB texture for the given rectangle in NDC coordinates
float GetMinDepthFromHZB(float4 rectMinMaxUV, Texture2D<float> HZBTexture, float2 hzbDimensions, SamplerState pointClampSampler)
{
    float4x4 HZBKernel = Get4x4HZBKernel(rectMinMaxUV, HZBTexture, hzbDimensions, pointClampSampler);

    float minDepth = Min4(
        Min4(HZBKernel[0][0], HZBKernel[1][0], HZBKernel[2][0], HZBKernel[3][0]),
        Min4(HZBKernel[0][1], HZBKernel[1][1], HZBKernel[2][1], HZBKernel[3][1]),
        Min4(HZBKernel[0][2], HZBKernel[1][2], HZBKernel[2][2], HZBKernel[3][2]),
        Min4(HZBKernel[0][3], HZBKernel[1][3], HZBKernel[2][3], HZBKernel[3][3])
    );

    return minDepth;
}

#endif // _HZB_H_
