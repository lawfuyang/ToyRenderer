#include "toyrenderer_common.hlsli"
#include "ShaderInterop.h"

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"
#include "DDGIRootConstants.hlsl"
#include "rtxgi/ddgi/DDGIVolumeDescGPU.h"

cbuffer GIProbeVisualizationUpdateConstsBuffer : register(b0) { GIProbeVisualizationUpdateConsts g_GIProbeVisualizationUpdateConsts; }
RWStructuredBuffer<float3> g_OutProbePositions : register(u0);
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t0);
Texture2DArray<float4> g_ProbeData : register(t1);

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_UpdateProbePositions(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Get the DDGIVolume index from root/push constants
    uint volumeIndex = GetDDGIVolumeIndex();

    // Load and unpack the DDGIVolume's constants
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);

    // Get the number of probes
    uint numProbes = (volume.probeCounts.x * volume.probeCounts.y * volume.probeCounts.z);

    // Early out: processed all probes, a probe doesn't exist for this thread
    if (dispatchThreadID.x >= numProbes)
        return;

    // Get the probe's grid coordinates
    float3 probeCoords = DDGIGetProbeCoords(dispatchThreadID.x, volume);

    // Get the probe's world position from the probe index
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);

    // Get the probe radius
    float probeRadius = g_GIProbeVisualizationUpdateConsts.m_ProbeRadius;

    // Get the instance offset (where one volume's probes end and another begin)
    // TODO: multiple volumes
    uint instanceOffset = 0;

    // Set the probe's transform
    g_OutProbePositions[(instanceOffset + dispatchThreadID.x)] = probeWorldPosition;
}

cbuffer GIProbeVisualizationConstsBuffer : register(b0) { GIProbeVisualizationConsts g_GIProbeVisualizationConsts; }
StructuredBuffer<float3> g_InProbePositions : register(t0);

void VS_Main
(
    in float3 inPosition : POSITION,
    in float3 inNormal : NORMAL,
    in float2 inTexCoord : TEXCOORD,
    out float4 outPosition : SV_POSITION,
    out float3 outNormal : TEXCOORD0,
    out float2 outTexCoord : TEXCOORD1
)
{
    outPosition = float4(inPosition, 1.0f);
    outNormal = inNormal;
    outTexCoord = inTexCoord;
}

void PS_Main
(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : TEXCOORD0,
    in float2 inTexCoord : TEXCOORD1,
    out float4 outColor : SV_Target
)
{
    outColor = 0;
}
