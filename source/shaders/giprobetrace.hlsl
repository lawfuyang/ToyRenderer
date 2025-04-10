#include "toyrenderer_common.hlsli"

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"
#include "DDGIRootConstants.hlsl"
#include "rtxgi/ddgi/DDGIVolumeDescGPU.h"
#include "../Irradiance.hlsl"

#include "lightingcommon.hlsli"
#include "raytracingcommon.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeTraceConstsBuffer : register(b0) { GIProbeTraceConsts g_GIProbeTraceConsts; }
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t0);
Texture2DArray<float4> g_ProbeData : register(t1);
Texture2DArray<float4> g_ProbeIrradiance : register(t2);
Texture2DArray<float4> g_ProbeDistance : register(t3);
RaytracingAccelerationStructure g_SceneTLAS : register(t4);
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t5);
StructuredBuffer<RawVertexFormat> g_GlobalVertexBuffer : register(t6);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t7);
StructuredBuffer<uint> g_GlobalIndexIDsBuffer : register(t8);
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t9);
RWTexture2DArray<float4> g_OutRayData : register(u0);
Texture2D g_Textures[] : register(t0, space1);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror
sampler g_LinearWrapSampler : register(s4); // Linear Wrap

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_ProbeTrace(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // TODO: multiple volumes
    uint volumeIndex = 0;
    
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);

    uint rayIndex = dispatchThreadID.x; // index of the ray to trace for this probe
    uint probePlaneIndex = dispatchThreadID.y; // index of this probe within the plane of probes
    uint planeIndex = dispatchThreadID.z; // index of the plane this probe is part of
    int probesPerPlane = DDGIGetProbesPerPlane(volume.probeCounts);
    int probeIndex = (planeIndex * probesPerPlane) + probePlaneIndex;
    float probeState = DDGILoadProbeState(probeIndex, g_ProbeData, volume);

    // Early out: do not shoot rays when the probe is inactive *unless* it is one of the "fixed" rays used by probe classification
    if (probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE && rayIndex >= RTXGI_DDGI_NUM_FIXED_RAYS)
    {
        return;
    }
    
    float3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);
    probeIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);
    float3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, volume);
    
    RayDesc radianceRayDesc;
    radianceRayDesc.Origin = probeWorldPosition;
    radianceRayDesc.Direction = probeRayDirection;
    radianceRayDesc.TMin = 0.0f;
    radianceRayDesc.TMax = volume.probeMaxRayDistance;
    
    const uint kRadianceRayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    
    // TODO: take into account alpha mask & transparency
    RayQuery<kRadianceRayFlags> radianceRayQuery;
    radianceRayQuery.TraceRayInline(g_SceneTLAS, kRadianceRayFlags, 0xFF, radianceRayDesc);
    radianceRayQuery.Proceed();
    
    uint3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, probeIndex, volume);
    
    // The ray missed. Store the miss radiance, set the hit distance to a large value, and exit early.
    if (radianceRayQuery.CommittedStatus() == COMMITTED_NOTHING)
    {
        // Store the ray miss
        // TODO: evaluate proper sky radiance
        float3 missRadiance = float3(1, 1, 1);
        DDGIStoreProbeRayMiss(g_OutRayData, outputCoords, volume, missRadiance);
        return;
    }
    
    if (!radianceRayQuery.CommittedTriangleFrontFace())
    {
        // Store the ray backface hit
        DDGIStoreProbeRayBackfaceHit(g_OutRayData, outputCoords, volume, radianceRayQuery.CommittedRayT());
        return;
    }
    
    // Early out: a "fixed" ray hit a front facing surface. Fixed rays are not blended since their direction
    // is not random and they would bias the irradiance estimate. Don't perform lighting for these rays.
    if ((volume.probeRelocationEnabled || volume.probeClassificationEnabled) && rayIndex < RTXGI_DDGI_NUM_FIXED_RAYS)
    {
        // Store the ray front face hit distance (only)
        DDGIStoreProbeRayFrontfaceHit(g_OutRayData, outputCoords, volume, radianceRayQuery.CommittedRayT());
        return;
    }

    GetRayHitInstanceGBufferParamsArguments args;
    args.m_BasePassInstanceConstantsBuffer = g_BasePassInstanceConsts;
    args.m_MaterialDataBuffer = g_MaterialDataBuffer;
    args.m_MeshDataBuffer = g_MeshDataBuffer;
    args.m_GlobalIndexIDsBuffer = g_GlobalIndexIDsBuffer;
    args.m_GlobalVertexBuffer = g_GlobalVertexBuffer;
    args.m_Samplers = g_Samplers;
    
    float3 rayHitWorldPosition;
    GBufferParams rayHitGBufferParams = GetRayHitInstanceGBufferParams(radianceRayQuery, g_Textures, rayHitWorldPosition, args);
    
    float3 radiance = float3(0, 0, 0);
    
    RayDesc shadowRayDesc;
    shadowRayDesc.Origin = probeWorldPosition;
    shadowRayDesc.Direction = g_GIProbeTraceConsts.m_DirectionalLightVector;
    shadowRayDesc.TMin = 0.0f;
    shadowRayDesc.TMax = volume.probeMaxRayDistance;
    
    const uint kShadowRayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    
    // TODO: take into account alpha mask & transparency
    RayQuery<kShadowRayFlags> shadowRayQuery;
    shadowRayQuery.TraceRayInline(g_SceneTLAS, kShadowRayFlags, 0xFF, shadowRayDesc);
    shadowRayQuery.Proceed();
    
    const bool bShadowed = shadowRayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (!bShadowed)
    {
        // direct lighting
        radiance = EvaluateDirectionalLight(rayHitGBufferParams, probeWorldPosition, rayHitWorldPosition, g_GIProbeTraceConsts.m_DirectionalLightVector);
    }
    
    float3 irradiance = float3(0, 0, 0);
    
    float volumeBlendWeight = DDGIGetVolumeBlendWeight(rayHitWorldPosition, volume);
    if (volumeBlendWeight > 0.0f)
    {
        float3 surfaceBias = DDGIGetSurfaceBias(rayHitGBufferParams.m_Normal, radianceRayDesc.Direction, volume);
    
        DDGIVolumeResources volumeResources;
        volumeResources.probeIrradiance = g_ProbeIrradiance;
        volumeResources.probeDistance = g_ProbeDistance;
        volumeResources.probeData = g_ProbeData;
        volumeResources.bilinearSampler = g_LinearWrapSampler;
    
        // Indirect Lighting (recursive)
        irradiance = DDGIGetVolumeIrradiance(rayHitWorldPosition, surfaceBias, rayHitGBufferParams.m_Normal, volume, volumeResources);
        irradiance *= volumeBlendWeight;

    }
    
    // Perfectly diffuse reflectors don't exist in the real world.
    // Limit the BRDF albedo to a maximum value to account for the energy loss at each bounce.
    const float kMaxAlbedo = 0.9f;
    
    // Store the final ray radiance and hit distance
    radiance += Diffuse_Lambert(min(rayHitGBufferParams.m_Albedo.rgb, float3(kMaxAlbedo, kMaxAlbedo, kMaxAlbedo)) * irradiance);
    DDGIStoreProbeRayFrontfaceHit(g_OutRayData, outputCoords, volume, saturate(radiance), radianceRayQuery.CommittedRayT());
}
