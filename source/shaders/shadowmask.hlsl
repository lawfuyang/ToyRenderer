#include "common.hlsli"
#include "shadowfiltering.hlsl"

#include "shared/ShadowMaskStructs.h"

cbuffer g_PassConstantsBuffer : register(b0) { ShadowMaskConsts g_ShadowMaskConsts; }
cbuffer g_PassConstantsBuffer : register(b0) { HardwareRaytraceConsts g_HardwareRaytraceConsts; }
Texture2D g_DepthBuffer : register(t0);
Texture2DArray g_DirLightShadowDepthTexture : register(t1);
RaytracingAccelerationStructure g_SceneTLAS : register(t1);
sampler g_PointClampSampler : register(s0);
SamplerComparisonState g_PointComparisonLessSampler : register(s1);
SamplerComparisonState g_LinearComparisonLessSampler : register(s2);
RWTexture2D<float4> g_ShadowMaskOutput : register(u0);

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float outColor : SV_Target
)
{
    float depth = g_DepthBuffer[inPosition.xy].x;
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

struct HitInfo
{
    bool m_Missed;
};

[shader("raygeneration")]
void RT_RayGen()
{
    uint2 rayIdx = DispatchRaysIndex().xy;
    float depth = g_DepthBuffer[rayIdx.xy].x;
    
    if (depth == kFarDepth)
    {
        return;
    }
    
    float2 screenUV = (rayIdx + float2(0.5f, 0.5f)) / g_HardwareRaytraceConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_HardwareRaytraceConsts.m_InvViewProjMatrix);
    
    const uint kRayFlags =
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
    
    RayDesc rayDesc;
    
    // TODO: offset abit based on normal to offset shadow acne
    // TODO: also based on screenspace distance? acne is worse over distance
    rayDesc.Origin = worldPosition;
    
    rayDesc.Direction = g_HardwareRaytraceConsts.m_DirectionalLightDirection;
    rayDesc.TMin = 0.1f;
    rayDesc.TMax = kKindaBigNumber;
    
    HitInfo payload;
    payload.m_Missed = false;
    
    TraceRay(
        g_SceneTLAS,
        kRayFlags,
        0xFF,
        0,
        0,
        0,
        rayDesc,
        payload);
    
    g_ShadowMaskOutput[rayIdx] = payload.m_Missed ? 1.0f : 0.0f;
}

[shader("miss")]
void RT_Miss(inout HitInfo payload : SV_RayPayload)
{
    payload.m_Missed = true;
}
