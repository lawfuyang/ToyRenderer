#include "toyrenderer_common.hlsli"

#include "lightingcommon.hlsli"
#include "culling.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeVisualizationUpdateConstsBuffer : register(b0) { GIProbeVisualizationUpdateConsts g_GIProbeVisualizationUpdateConsts; }
RWStructuredBuffer<float3> g_OutProbePositions : register(u0);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_OutProbeIndirectArgs : register(u1);
RWStructuredBuffer<uint> g_OutInstanceIndexToProbeIndex : register(u2);
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t10);
RWTexture2DArray<float4> g_RTDDIProbeData : register(u10);
Texture2D g_HZB : register(t0);
SamplerState g_LinearClampMinReductionSampler : register(s0);

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_VisualizeGIProbesCulling(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint probeIndex = dispatchThreadID.x;
    
    if (probeIndex >= g_GIProbeVisualizationUpdateConsts.m_NumProbes)
    {
        return;
    }
    
    // TODO: multiple volumes
    uint volumeIndex = 0;
    
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);
    float probeState = DDGILoadProbeState(probeIndex, g_RTDDIProbeData, volume);
    
    if (g_GIProbeVisualizationUpdateConsts.m_bHideInactiveProbes && probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE)
    {
        return;
    }
    
    float3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_RTDDIProbeData);
    
    float3 probeViewSpacePosition = mul(float4(probeWorldPosition, 1.0f), g_GIProbeVisualizationUpdateConsts.m_WorldToView).xyz;
    probeViewSpacePosition.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    if (!FrustumCull(probeViewSpacePosition, g_GIProbeVisualizationUpdateConsts.m_ProbeRadius, g_GIProbeVisualizationUpdateConsts.m_Frustum))
    {
        return;
    }
    
    OcclusionCullArguments occlusionCullArguments;
    occlusionCullArguments.m_SphereCenterViewSpace = probeViewSpacePosition;
    occlusionCullArguments.m_Radius = g_GIProbeVisualizationUpdateConsts.m_ProbeRadius;
    occlusionCullArguments.m_NearPlane = g_GIProbeVisualizationUpdateConsts.m_NearPlane;
    occlusionCullArguments.m_P00 = g_GIProbeVisualizationUpdateConsts.m_P00;
    occlusionCullArguments.m_P11 = g_GIProbeVisualizationUpdateConsts.m_P11;
    occlusionCullArguments.m_HZB = g_HZB;
    occlusionCullArguments.m_HZBDimensions = g_GIProbeVisualizationUpdateConsts.m_HZBDimensions;
    occlusionCullArguments.m_LinearClampMinReductionSampler = g_LinearClampMinReductionSampler;
    
    if (!OcclusionCull(occlusionCullArguments))
    {
        return;
    }
    
    uint outInstanceIndex;
    InterlockedAdd(g_OutProbeIndirectArgs[0].m_InstanceCount, 1, outInstanceIndex);

    // Set the probe's transform
    g_OutProbePositions[outInstanceIndex] = probeWorldPosition;
    g_OutInstanceIndexToProbeIndex[outInstanceIndex] = probeIndex;
}

cbuffer GIProbeVisualizationConstsBuffer : register(b0) { GIProbeVisualizationConsts g_GIProbeVisualizationConsts; }
StructuredBuffer<float3> g_InProbePositions : register(t0);
Texture2DArray<float4> g_VisProbeData : register(t1);
Texture2DArray<float4> g_VisProbeIrradiance : register(t2);
Texture2DArray<float4> g_VisProbeDistance : register(t3);
StructuredBuffer<DDGIVolumeDescGPUPacked> g_VisDDGIVolumes : register(t4);
StructuredBuffer<uint> g_InInstanceIndexToProbeIndex : register(t5);
sampler g_LinearWrapSampler : register(s0);

void VS_VisualizeGIProbes
(
    in uint inInstanceID : SV_InstanceID,
    in float3 inPosition : POSITION,
    in float3 inNormal : NORMAL,
    in float2 inTexCoord : TEXCOORD,
    out float4 outPosition : SV_POSITION,
    out float3 outNormal : TEXCOORD0,
    out float2 outTexCoord : TEXCOORD1,
    out float3 outWorldPosition : TEXCOORD2,
    nointerpolation out uint outInstanceID : TEXCOORD3)
{    
    float3 worldPosition = g_InProbePositions[inInstanceID] + (g_GIProbeVisualizationConsts.m_ProbeRadius * inNormal);
    
    outPosition = mul(float4(worldPosition, 1.0f), g_GIProbeVisualizationConsts.m_WorldToClip);
    outNormal = inNormal;
    outTexCoord = inTexCoord;
    outWorldPosition = worldPosition;
    outInstanceID = inInstanceID;
}

void PS_VisualizeGIProbes
(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : TEXCOORD0,
    in float2 inTexCoord : TEXCOORD1,
    in float3 inWorldPosition : TEXCOORD2,
    in uint inInstanceID : TEXCOORD3,
    out float4 outColor : SV_Target0)
{    
    // TODO: multiple volumes
    uint volumeIndex = 0;
    
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_VisDDGIVolumes[volumeIndex]);
    
    uint probeIndex = g_InInstanceIndexToProbeIndex[inInstanceID];
    float3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);
    probeIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);
    float3 probePosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_VisProbeData);
    float3 sampleDirection = normalize(inWorldPosition - probePosition);
    float2 octantCoords = DDGIGetOctahedralCoordinates(sampleDirection);
    float3 uv = DDGIGetProbeUV(probeIndex, octantCoords, volume.probeNumIrradianceInteriorTexels, volume);
    
    float3 irradiance = g_VisProbeIrradiance.SampleLevel(g_LinearWrapSampler, uv, 0).rgb;
    
    // Decode the tone curve
    float3 exponent = volume.probeIrradianceEncodingGamma * 0.5f;
    irradiance = pow(irradiance, exponent);

    // Go back to linear irradiance
    irradiance *= irradiance;

    // Multiply by the area of the integration domain (2PI) to complete the irradiance estimate. Divide by PI to normalize for the display.
    irradiance *= 2.f;

    // Adjust for energy loss due to reduced precision in the R10G10B10A2 irradiance texture format
    if (volume.probeIrradianceFormat == RTXGI_DDGI_VOLUME_TEXTURE_FORMAT_U32)
    {
        irradiance *= 1.0989f;
    }
    
    outColor = float4(irradiance, 1);
}
