#ifndef _SHADOWFILTERING_HLSL_
#define _SHADOWFILTERING_HLSL_

#define AMD_SHADOWFX_FILTER_SIZE_7                          7
#define AMD_SHADOWFX_FILTER_SIZE_9                          9
#define AMD_SHADOWFX_FILTER_SIZE_11                         11
#define AMD_SHADOWFX_FILTER_SIZE_13                         13
#define AMD_SHADOWFX_FILTER_SIZE_15                         15

#define AMD_SHADOWFX_FILTER_SIZE AMD_SHADOWFX_FILTER_SIZE_7
#define AMD_SHADOWS_FILTER_RADIUS ( AMD_SHADOWFX_FILTER_SIZE/2 )
#define FS AMD_SHADOWFX_FILTER_SIZE   // abbreviation for Filter Size
#define FR AMD_SHADOWS_FILTER_RADIUS  // abbreviation for Filter Radius

#if   (AMD_SHADOWFX_FILTER_SIZE == AMD_SHADOWFX_FILTER_SIZE_7)
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_7_FIXED.inc"
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_7_POISSON.inc"
#elif (AMD_SHADOWFX_FILTER_SIZE == AMD_SHADOWFX_FILTER_SIZE_9)
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_9_FIXED.inc"
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_9_POISSON.inc"
#elif (AMD_SHADOWFX_FILTER_SIZE == AMD_SHADOWFX_FILTER_SIZE_11)
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_11_FIXED.inc"
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_11_POISSON.inc"
#elif (AMD_SHADOWFX_FILTER_SIZE == AMD_SHADOWFX_FILTER_SIZE_13)
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_13_FIXED.inc"
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_13_POISSON.inc"
#elif (AMD_SHADOWFX_FILTER_SIZE == AMD_SHADOWFX_FILTER_SIZE_15)
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_15_FIXED.inc"
#include "extern/amd/shadowfx/amd_shadowfx/src/Shaders/AMD_SHADOWFX_FILTER_SIZE_15_POISSON.inc"
#endif

struct ShadowFilteringParams
{
    float3 m_WorldPosition;
    float3 m_CameraPosition;
    float4 m_CSMDistances;
    float4x4 m_DirLightViewProj[4];
    float m_InvShadowMapResolution;
    
    Texture2DArray m_DirLightShadowDepthTexture;
    sampler m_PointClampSampler;
    SamplerComparisonState m_PointComparisonLessSampler;
    SamplerComparisonState m_LinearComparisonLessSampler;
};

float CubicBezierCurve(float v1, float v2, float v3, float v4, float t)
{
    return (1.0 - t) * (1.0 - t) * (1.0 - t) * v1 + 3.0f * (1.0 - t) * (1.0 - t) * t * v2 + 3.0f * t * t * (1.0 - t) * v3 + t * t * t * v4;
}

float PercentageCloserSoftShadows(float depth, float blockerDepth)
{
    static const float kSunArea = 8.0f;
    
    // compute ratio using formulas from PCSS article http://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
    return pow(saturate((depth - blockerDepth) * kSunArea / blockerDepth), 0.5f);
}

float2 UniformBlockerSearch(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    float2 blockerInfo = { 0.0f, 0.0f };

    [loop]
    for (int row = -BFR; row <= BFR; row += 2) // BFR is Blocker Filter Size
    {
        [loop]
        for (int col = -BFR; col <= BFR; col += 2)
        {
            float4 depth = params.m_DirLightShadowDepthTexture.GatherRed(params.m_PointClampSampler, float3(shadowTexCoord.xy + float2(col, row) * params.m_InvShadowMapResolution, CSMIndex));
            float4 blocker = select((shadowTexCoord.z).xxxx <= depth, (0.0).xxxx, (1.0).xxxx);
            blockerInfo.x += dot(blocker, (1.0).xxxx);
            blockerInfo.y += dot(depth, blocker);
        }
    }

    if (blockerInfo.x > 0.0)
        blockerInfo.y /= blockerInfo.x;

    blockerInfo.x /= ((BFS + 1) * (BFS + 1));

    return blockerInfo;
}

float2 PoissonBlockerSearch(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    float2 blockerInfo = { 0.0f, 0.0f };

    [unroll]
    for (uint i = 0; i < g_PoissonSamplesCount; i++)
    {
        float4 depth = params.m_DirLightShadowDepthTexture.GatherRed(params.m_PointClampSampler, float3(shadowTexCoord.xy + g_PoissonSamples[i].xy * params.m_InvShadowMapResolution, CSMIndex));
        float4 blocker = select((shadowTexCoord.z).xxxx <= depth, (0.0).xxxx, (1.0).xxxx);
        blockerInfo.x += dot(blocker, (1.0).xxxx);
        blockerInfo.y += dot(depth, blocker);
    }

    if (blockerInfo.x > 0.0)
        blockerInfo.y /= blockerInfo.x;

    blockerInfo.x /= (g_PoissonSamplesCount * 4.0f); // each jittered sample actually fetches 4 samples through gather4

    return blockerInfo;
}

float CalculateFilterWeight(float x, float y, float ratio)
{
    if (x < -FS || x > FS)
        return 0.0f;
    if (y < -FS || y > FS)
        return 0.0f;

    float sigma = (FS - 1) * 0.5f;

    float lowGaussianWeight = exp(-(x * x + y * y) / (0.25 * sigma * 0.25 * sigma));
    float mediumGaussianWeight = exp(-(x * x + y * y) / (0.5 * sigma * 0.5 * sigma));
    float highGaussianWeight = exp(-(x * x + y * y) / (0.75 * sigma * 0.75 * sigma));
    float ultraGaussianWeight = exp(-(x * x + y * y) / (sigma * sigma));

    return CubicBezierCurve(lowGaussianWeight, mediumGaussianWeight, highGaussianWeight, ultraGaussianWeight, ratio);
}

float FetchFilterWeight(int r, int c, float ratio)
{
    if (r < 0 || r >= FS)
        return 0.0f;
    if (c < 0 || c >= FS)
        return 0.0f;
    return CubicBezierCurve(g_lowGaussianWeight[r][c], g_mediumGaussianWeight[r][c], g_highGaussianWeight[r][c], g_ultraGaussianWeight[r][c], ratio);
}

float Manual1x1PCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    return params.m_DirLightShadowDepthTexture.SampleCmpLevelZero(params.m_PointComparisonLessSampler, float3(shadowTexCoord.xy, CSMIndex), shadowTexCoord.z);
}

float Manual3X3PCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    static const float2 kKernal[] =
    {
        float2(-1.0f, -1.0f), float2(0.0f, -1.0f), float2(1.0f, -1.0f),
        float2(-1.0f, 0.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f),
        float2(-1.0f, 1.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f)
    };
    
    const float dx = params.m_InvShadowMapResolution;
    
    float percentLit = 0.0f;
    
    [unroll]
    for (uint i = 0; i < 9; ++i)
    {
        percentLit += Manual1x1PCF(params, shadowTexCoord + float3(kKernal[i] * dx, 0.0f), CSMIndex);
    }
    
    return percentLit / 9.0f;
}

//--------------------------------------------------------------------------------------
// CONTACT shadow filtering https://github.com/GPUOpen-Effects/ShadowFX
//--------------------------------------------------------------------------------------
float ContactFixedPCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    float2 blockerInfo = UniformBlockerSearch(params, shadowTexCoord, CSMIndex);
    float blockerCount = blockerInfo.x;
    float blockerDepth = blockerInfo.y;
  
    // compute ratio using formulas from PCSS
    float ratio = 0.0f;
    if (blockerCount > 0.0 && blockerCount < 1.0f)
    {
        ratio = PercentageCloserSoftShadows(shadowTexCoord.z, blockerDepth);
    }
    else // Early out - fully lit or fully in shadow depends on blockerCount
    {
        return 1.0f - blockerCount;
    }

    float accumulatedShadow = 0.0f;
    float accumulatedWeight = 0.0f;

    [unroll]
    for (float row = -FR; row <= FR; row += 1)
    {
        [unroll]
        for (float col = -FR; col <= FR; col += 1)
        {
            float weight = FetchFilterWeight(row + FR, col + FR, ratio);
            float shadow = params.m_DirLightShadowDepthTexture.SampleCmpLevelZero(params.m_LinearComparisonLessSampler, float3(shadowTexCoord.xy + float2(col, row) * params.m_InvShadowMapResolution, CSMIndex), shadowTexCoord.z);

            accumulatedShadow += shadow * weight;
            accumulatedWeight += weight;
        }
    }

    accumulatedShadow = accumulatedShadow / (accumulatedWeight);

    return accumulatedShadow;
}

float ContactPoissonPCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    float2 blockerInfo = PoissonBlockerSearch(params, shadowTexCoord, CSMIndex);
    float blockerCount = blockerInfo.x;
    float blockerDepth = blockerInfo.y;
  
    // compute ratio using formulas from PCSS
    float ratio = 0.0f;
    if (blockerCount > 0.0 && blockerCount < 1.0f)
    {
        ratio = PercentageCloserSoftShadows(shadowTexCoord.z, blockerDepth);
    }
    else // Early out - fully lit or fully in shadow depends on blockerCount
    {
        return 1.0f - blockerCount;
    }

    float accumulatedShadow = 0.0f;
    float accumulatedWeight = 0.0f;
    
    [unroll]
    for (uint i = 0; i < g_PoissonSamplesCount; i += 1)
    {
        float weight = CalculateFilterWeight(g_PoissonSamples[i].x, g_PoissonSamples[i].y, ratio);
        float shadow = params.m_DirLightShadowDepthTexture.SampleCmpLevelZero(params.m_LinearComparisonLessSampler, float3(shadowTexCoord.xy + g_PoissonSamples[i] * params.m_InvShadowMapResolution, CSMIndex), shadowTexCoord.z);

        accumulatedShadow += shadow * weight;
        accumulatedWeight += weight;
    }

    accumulatedShadow = accumulatedShadow / accumulatedWeight;

    return accumulatedShadow;
}

float ShadowFiltering(ShadowFilteringParams params)
{
    float cameraDistance = length(params.m_CameraPosition - params.m_WorldPosition);
    
    // beyond last cascade
    if (cameraDistance >= params.m_CSMDistances.w)
        return 0.0f;
    
    int CSMIndex = 0;
    for (uint i = 0; i < 4; ++i)
    {
        if (cameraDistance < params.m_CSMDistances[i])
        {
            CSMIndex = i;
            break;
        }
    }
    
    float4 shadowTexCoord = mul(float4(params.m_WorldPosition, 1.0f), params.m_DirLightViewProj[CSMIndex]);
    shadowTexCoord.xyz = shadowTexCoord.xyz / shadowTexCoord.w;
    
    // Re-scale to 0-1
    shadowTexCoord.x = (1.0f + shadowTexCoord.x) * 0.5f;
    shadowTexCoord.y = (1.0f - shadowTexCoord.y) * 0.5f;
    
#if 0
    return Manual1x1PCF(params, shadowTexCoord.xyz, CSMIndex);
#elif 0
    return Manual3X3PCF(params, shadowTexCoord.xyz, CSMIndex);
#elif 1
    return ContactFixedPCF(params, shadowTexCoord.xyz, CSMIndex);
#elif 1
    return ContactPoissonPCF(params, shadowTexCoord.xyz, CSMIndex);
#endif
}

#endif // _SHADOWFILTERING_HLSL_
