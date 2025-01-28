#include "common.hlsli"
#include "lightingcommon.hlsli"

#include "shared/ShadowMaskStructs.h"

cbuffer g_PassConstantsBuffer : register(b0) { ShadowMaskConsts g_ShadowMaskConsts; }
Texture2D g_DepthBuffer : register(t0);
RaytracingAccelerationStructure g_SceneTLAS : register(t1);
Texture2D<uint4> g_GBufferA : register(t2);
RWTexture2D<float4> g_ShadowMaskOutput : register(u0);

struct RayPayload
{
    float m_HitT;
    
    bool IsHit()
    {
        return m_HitT >= 0.0f;
    }
};

struct IntersectionAttributes
{
    float2 uv;
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
    
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[rayIdx.xy], gbufferParams);
    
    float2 screenUV = (rayIdx + float2(0.5f, 0.5f)) / g_ShadowMaskConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_ShadowMaskConsts.m_ClipToWorld);
    
    const uint kRayFlags =
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
    
    // empirical offset to remove shadow acne
    float3 rayOriginOffset = gbufferParams.m_Normal * 0.01f;
    
    RayDesc rayDesc;
    rayDesc.Origin = worldPosition + rayOriginOffset;
    rayDesc.Direction = g_ShadowMaskConsts.m_DirectionalLightDirection;
    rayDesc.TMin = 0.1f;
    rayDesc.TMax = kKindaBigNumber;
    
    RayPayload payload;
    payload.m_HitT = -1.0f;
    
    TraceRay(
        g_SceneTLAS,
        kRayFlags,
        0xFF,
        0,
        0,
        0,
        rayDesc,
        payload);
    
    g_ShadowMaskOutput[rayIdx] = payload.IsHit() ? 0.0f : 1.0f;
}

[shader("miss")]
void RT_Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.m_HitT = -1.0f;
}

[shader("closesthit")]
void RT_ClosestHit(inout RayPayload payload : SV_RayPayload, in IntersectionAttributes attributes : SV_IntersectionAttributes)
{
    payload.m_HitT = RayTCurrent();
}
