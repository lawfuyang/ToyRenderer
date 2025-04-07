#include "toyrenderer_common.hlsli"

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"
#include "DDGIRootConstants.hlsl"
#include "rtxgi/ddgi/DDGIVolumeDescGPU.h"

#include "culling.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeVisualizationUpdateConstsBuffer : register(b0) { GIProbeVisualizationUpdateConsts g_GIProbeVisualizationUpdateConsts; }
RWStructuredBuffer<float3> g_OutProbePositions : register(u0);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_OutProbeIndirectArgs : register(u1);
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t0);
Texture2DArray<float4> g_ProbeData : register(t1);
Texture2D g_HZB : register(t2);
SamplerState g_LinearClampMinReductionSampler : register(s0);

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_GenerateIndirectArgs(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Early out: processed all probes, a probe doesn't exist for this thread
    if (dispatchThreadID.x >= g_GIProbeVisualizationUpdateConsts.m_NumProbes)
    {
        return;
    }
    
    // Get the DDGIVolume index from root/push constants
    // TODO: multiple volumes
    uint volumeIndex = 0;

    // Load and unpack the DDGIVolume's constants
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);

    // Get the probe's grid coordinates
    float3 probeCoords = DDGIGetProbeCoords(dispatchThreadID.x, volume);

    // Get the probe's world position from the probe index
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);
    
    float3 probeViewSpacePosition = mul(float4(probeWorldPosition, 1.0f), g_GIProbeVisualizationUpdateConsts.m_WorldToView).xyz;
    probeViewSpacePosition.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    if (!FrustumCull(probeViewSpacePosition, g_GIProbeVisualizationUpdateConsts.m_ProbeRadius, g_GIProbeVisualizationUpdateConsts.m_Frustum))
    {
        return;
    }
    
    if (!OcclusionCull(probeViewSpacePosition,
                       g_GIProbeVisualizationUpdateConsts.m_ProbeRadius,
                       g_GIProbeVisualizationUpdateConsts.m_NearPlane,
                       g_GIProbeVisualizationUpdateConsts.m_P00,
                       g_GIProbeVisualizationUpdateConsts.m_P11,
                       g_HZB,
                       g_GIProbeVisualizationUpdateConsts.m_HZBDimensions,
                       g_LinearClampMinReductionSampler))
    {
        return;
    }
    
    // debug probes that are too far can't be seen properly anyway
    if (length(probeViewSpacePosition) > g_GIProbeVisualizationUpdateConsts.m_MaxDebugProbeDistance)
    {
        return;
    }
    
    uint outInstanceIndex;
    InterlockedAdd(g_OutProbeIndirectArgs[0].m_InstanceCount, 1, outInstanceIndex);

    // Set the probe's transform
    g_OutProbePositions[outInstanceIndex] = probeWorldPosition;
}

cbuffer GIProbeVisualizationConstsBuffer : register(b0) { GIProbeVisualizationConsts g_GIProbeVisualizationConsts; }
StructuredBuffer<float3> g_InProbePositions : register(t0);

void VS_GIProbes
(
    in uint inInstanceID : SV_InstanceID,
    in float3 inPosition : POSITION,
    in float3 inNormal : NORMAL,
    in float2 inTexCoord : TEXCOORD,
    out float4 outPosition : SV_POSITION,
    out float3 outNormal : TEXCOORD0,
    out float2 outTexCoord : TEXCOORD1
)
{
    float3 worldPosition = g_InProbePositions[inInstanceID] + (g_GIProbeVisualizationConsts.m_ProbeRadius * inNormal);
    
    outPosition = mul(float4(worldPosition, 1.0f), g_GIProbeVisualizationConsts.m_WorldToClip);
    outNormal = inNormal;
    outTexCoord = inTexCoord;
}

void PS_GIProbes
(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : TEXCOORD0,
    in float2 inTexCoord : TEXCOORD1,
    out float4 outColor : SV_Target0
)
{
    outColor = float4(1, 1, 1, 1);
}
