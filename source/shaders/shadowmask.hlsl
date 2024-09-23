#include "common.hlsli"
#include "shadowfiltering.hlsl"

#include "shared/ShadowMaskStructs.h"

cbuffer g_PassConstantsBuffer : register(b0) { ShadowMaskConsts g_ShadowMaskConsts; }
Texture2D g_DepthBuffer : register(t0);
Texture2DArray g_DirLightShadowDepthTexture : register(t1);
sampler g_PointClampSampler : register(s0);
SamplerComparisonState g_PointComparisonLessSampler : register(s1);
SamplerComparisonState g_LinearComparisonLessSampler : register(s2);

/*
float ScreenSpaceShadows(float3 worldPosition, float3 lightDirection, Texture2D<float> depthTexture, int stepCount, float rayLength, float ditherOffset)
{
    float4 rayStartPS = mul(float4(worldPosition, 1), cView.WorldToClip);
    float4 rayDirPS = mul(float4(-lightDirection * rayLength, 0), cView.WorldToClip);
    float4 rayEndPS = rayStartPS + rayDirPS;
    rayStartPS.xyz /= rayStartPS.w;
    rayEndPS.xyz /= rayEndPS.w;
    float3 rayStep = rayEndPS.xyz - rayStartPS.xyz;
    float stepSize = 1.0f / stepCount;

    float4 rayDepthClip = rayStartPS + mul(float4(0, 0, rayLength, 0), cView.ViewToClip);
    rayDepthClip.xyz /= rayDepthClip.w;
    float tolerance = abs(rayDepthClip.z - rayStartPS.z) * stepSize * 2;

    float occlusion = 0.0f;
    float hitStep = -1.0f;

    float n = stepSize * ditherOffset + stepSize;

	[unroll]
    for (uint i = 0; i < stepCount; ++i)
    {
        float3 rayPos = rayStartPS.xyz + n * rayStep;
        float depth = depthTexture.SampleLevel(sPointClamp, ClipToUV(rayPos.xy), 0).r;
        float diff = rayPos.z - depth;

        bool hit = abs(diff + tolerance) < tolerance;
        hitStep = hit && hitStep < 0.0f ? n : hitStep;
        n += stepSize;
    }
    if (hitStep > 0.0f)
    {
        float2 hitUV = rayStartPS.xy + n * rayStep.xy;
        occlusion = ScreenFade(ClipToUV(hitUV));
    }
    return 1.0f - occlusion;
}
*/

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float outColor : SV_Target
)
{
    // Get the depth value from the depth buffer
    float depth = g_DepthBuffer[inPosition.xy].x;
    
    // Convert screen UV coordinates and depth to world position
    float3 worldPosition = ScreenUVToWorldPosition(inUV, depth, g_ShadowMaskConsts.m_InvViewProjMatrix);
    
    // Set up the parameters for shadow filtering
    ShadowFilteringParams shadowFilteringParams;
    shadowFilteringParams.m_WorldPosition = worldPosition;
    shadowFilteringParams.m_CameraPosition = g_ShadowMaskConsts.m_CameraOrigin;
    shadowFilteringParams.m_CSMDistances = g_ShadowMaskConsts.m_CSMDistances;
    shadowFilteringParams.m_DirLightViewProj = g_ShadowMaskConsts.m_DirLightViewProj;
    shadowFilteringParams.m_InvShadowMapResolution = g_ShadowMaskConsts.m_InvShadowMapResolution;
    shadowFilteringParams.m_DirLightShadowDepthTexture = g_DirLightShadowDepthTexture;
    shadowFilteringParams.m_PointClampSampler = g_PointClampSampler;
    shadowFilteringParams.m_PointComparisonLessSampler = g_PointComparisonLessSampler;
    shadowFilteringParams.m_LinearComparisonLessSampler = g_LinearComparisonLessSampler;
    
    // Perform shadow filtering and assign the result to the output color
    outColor = ShadowFiltering(shadowFilteringParams);
    
    if (g_ShadowMaskConsts.m_InversedDepth)
    {
        outColor = 1.0f - outColor;
    }
}
