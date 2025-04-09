#include "toyrenderer_common.hlsli"

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"
#include "DDGIRootConstants.hlsl"
#include "rtxgi/ddgi/DDGIVolumeDescGPU.h"

#include "lightingcommon.hlsli"
#include "raytracingcommon.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeTraceConstsBuffer : register(b0) { GIProbeTraceConsts g_GIProbeTraceConsts; }
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t0);
Texture2DArray<float4> g_ProbeData : register(t1);
RaytracingAccelerationStructure g_SceneTLAS : register(t2);
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t3);
StructuredBuffer<RawVertexFormat> g_GlobalVertexBuffer : register(t4);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t5);
StructuredBuffer<uint> g_GlobalIndexIDsBuffer : register(t6);
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t7);
RWTexture2DArray<float4> g_OutRayData : register(u0);
Texture2D g_Textures[] : register(t0, space1);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror

[numthreads(1, 1, 1)]
void CS_ProbeTrace(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // TODO: multiple volumes
    uint volumeIndex = 0;

    uint rayIndex = dispatchThreadID.x; // index of the ray to trace for this probe
    uint probePlaneIndex = dispatchThreadID.y; // index of this probe within the plane of probes
    uint planeIndex = dispatchThreadID.z; // index of the plane this probe is part of

    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);
    int probesPerPlane = DDGIGetProbesPerPlane(volume.probeCounts);
    
    int probeIndex = (planeIndex * probesPerPlane) + probePlaneIndex;
    float3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);
    float3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, volume);
    
    uint3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, probeIndex, volume);

    // Setup the probe ray
    RayDesc rayDesc;
    rayDesc.Origin = probeWorldPosition;
    rayDesc.Direction = probeRayDirection;
    rayDesc.TMin = 0.f;
    rayDesc.TMax = volume.probeMaxRayDistance;
    
    const uint kFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    
    // TODO: take into account alpha mask & transparency
    RayQuery<kFlags> rayQuery;
    rayQuery.TraceRayInline(g_SceneTLAS, kFlags, 0xFF, rayDesc);
    rayQuery.Proceed();
    
    // The ray missed. Store the miss radiance, set the hit distance to a large value, and exit early.
    if (rayQuery.CommittedStatus() == COMMITTED_NOTHING)
    {
        // Store the ray miss
        float3 missRadiance = float3(1, 1, 1);
        DDGIStoreProbeRayMiss(g_OutRayData, outputCoords, volume, missRadiance);
        return;
    }
    
    if (!rayQuery.CommittedTriangleFrontFace())
    {
        // Store the ray backface hit
        DDGIStoreProbeRayBackfaceHit(g_OutRayData, outputCoords, volume, rayQuery.CommittedRayT());
        return;
    }
    
    // Early out: a "fixed" ray hit a front facing surface. Fixed rays are not blended since their direction
    // is not random and they would bias the irradiance estimate. Don't perform lighting for these rays.
    if ((volume.probeRelocationEnabled || volume.probeClassificationEnabled) && rayIndex < RTXGI_DDGI_NUM_FIXED_RAYS)
    {
        // Store the ray front face hit distance (only)
        DDGIStoreProbeRayFrontfaceHit(g_OutRayData, outputCoords, volume, rayQuery.CommittedRayT());
        return;
    }
    
    // TODO: take into account alpha mask & transparency

    GetRayHitInstanceGBufferParamsArguments args;
    args.m_BasePassInstanceConstantsBuffer = g_BasePassInstanceConsts;
    args.m_MaterialDataBuffer = g_MaterialDataBuffer;
    args.m_MeshDataBuffer = g_MeshDataBuffer;
    args.m_GlobalIndexIDsBuffer = g_GlobalIndexIDsBuffer;
    args.m_GlobalVertexBuffer = g_GlobalVertexBuffer;
    args.m_Samplers = g_Samplers;
    
    float3 rayHitWorldPosition;
    GBufferParams gbufferParams = GetRayHitInstanceGBufferParams(rayQuery, g_Textures, rayHitWorldPosition, args);
    
    float3 lighting = EvaluateDirectionalLight(gbufferParams, probeWorldPosition, rayHitWorldPosition, g_GIProbeTraceConsts.m_DirectionalLightVector);
    
    // Store the final ray radiance and hit distance
    float3 radiance = float3(0, 1, 0);
    DDGIStoreProbeRayFrontfaceHit(g_OutRayData, outputCoords, volume, saturate(radiance), rayQuery.CommittedRayT());
}
