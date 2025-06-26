#include "toyrenderer_common.hlsli"

#include "lightingcommon.hlsli"
#include "raytracingcommon.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeTraceConstsBuffer : register(b0) { GIProbeTraceConsts g_GIProbeTraceConsts; }

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_ProbeTrace(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_DDGIVolumesIdx];
    Texture2DArray<float4> g_ProbeData = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_ProbeDataIdx];
    Texture2DArray<float4> g_ProbeIrradiance = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_ProbeIrradianceIdx];
    Texture2DArray<float4> g_ProbeDistance = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_ProbeDistanceIdx];
    RaytracingAccelerationStructure g_SceneTLAS = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_SceneTLASIdx];
    StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_BasePassInstanceConstsIdx];
    StructuredBuffer<RawVertexFormat> g_GlobalVertexBuffer = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_GlobalVertexBufferIdx];
    StructuredBuffer<MaterialData> g_MaterialDataBuffer = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_MaterialDataBufferIdx];
    StructuredBuffer<uint> g_GlobalIndexIDsBuffer = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_GlobalIndexIDsBufferIdx];
    StructuredBuffer<MeshData> g_MeshDataBuffer = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_MeshDataBufferIdx];
    RWTexture2DArray<float4> g_OutRayData = ResourceDescriptorHeap[g_GIProbeTraceConsts.m_OutRayDataIdx];
    
    sampler anisotropicSamplers[SamplerIdx_Count] =
    {
        SamplerDescriptorHeap[g_GIProbeTraceConsts.m_SamplersIdx + 0],
        SamplerDescriptorHeap[g_GIProbeTraceConsts.m_SamplersIdx + 1],
        SamplerDescriptorHeap[g_GIProbeTraceConsts.m_SamplersIdx + 2],
        SamplerDescriptorHeap[g_GIProbeTraceConsts.m_SamplersIdx + 3],
    };
    
    sampler linearWrapSampler = SamplerDescriptorHeap[g_GIProbeTraceConsts.m_SamplersIdx + 4];
    
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
    
    // TODO: take into account alpha mask & transparency
    RayQuery<RAY_FLAG_FORCE_OPAQUE> radianceRayQuery;
    radianceRayQuery.TraceRayInline(g_SceneTLAS, RAY_FLAG_NONE, 0xFF, radianceRayDesc);
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
    args.m_InstanceID = radianceRayQuery.CommittedInstanceID();
    args.m_PrimitiveIndex = radianceRayQuery.CommittedPrimitiveIndex();
    args.m_AttribBarycentrics = radianceRayQuery.CommittedTriangleBarycentrics();
    args.m_ObjectToWorld3x4 = radianceRayQuery.CommittedObjectToWorld3x4();
    args.m_BasePassInstanceConstantsBuffer = g_BasePassInstanceConsts;
    args.m_MaterialDataBuffer = g_MaterialDataBuffer;
    args.m_MeshDataBuffer = g_MeshDataBuffer;
    args.m_GlobalIndexIDsBuffer = g_GlobalIndexIDsBuffer;
    args.m_GlobalVertexBuffer = g_GlobalVertexBuffer;
    args.m_Samplers = anisotropicSamplers;
    
    float3 rayHitWorldPosition;
    GBufferParams rayHitGBufferParams = GetRayHitInstanceGBufferParams(args, rayHitWorldPosition);
    
    float3 radiance = float3(0, 0, 0);
    
    RayDesc shadowRayDesc;
    shadowRayDesc.Origin = probeWorldPosition;
    shadowRayDesc.Direction = g_GIProbeTraceConsts.m_DirectionalLightVector;
    shadowRayDesc.TMin = 0.0f;
    shadowRayDesc.TMax = kKindaBigNumber;
    
    // TODO: take into account alpha mask & transparency
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowRayQuery;
    shadowRayQuery.TraceRayInline(g_SceneTLAS, RAY_FLAG_NONE, 0xFF, shadowRayDesc);
    shadowRayQuery.Proceed();
    
    const bool bShadowed = shadowRayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (!bShadowed)
    {
        // direct lighting
        radiance = EvaluateDirectionalLight(rayHitGBufferParams, probeWorldPosition, rayHitWorldPosition, g_GIProbeTraceConsts.m_DirectionalLightVector, g_GIProbeTraceConsts.m_DirectionalLightStrength);
    }
    radiance += rayHitGBufferParams.m_Emissive;
    
    DDGIVolumeResources volumeResources;
    volumeResources.probeIrradiance = g_ProbeIrradiance;
    volumeResources.probeDistance = g_ProbeDistance;
    volumeResources.probeData = g_ProbeData;
    volumeResources.bilinearSampler = linearWrapSampler;
    
    GetDDGIIrradianceArguments irradianceArgs;
    irradianceArgs.m_WorldPosition = rayHitWorldPosition;
    irradianceArgs.m_VolumeDesc = volume;
    irradianceArgs.m_Normal = rayHitGBufferParams.m_Normal;
    irradianceArgs.m_ViewDirection = radianceRayDesc.Direction;
    irradianceArgs.m_DDGIVolumeResources = volumeResources;
    
    // Indirect Lighting (recursive)
    float3 irradiance = GetDDGIIrradiance(irradianceArgs);
    
    // Perfectly diffuse reflectors don't exist in the real world.
    // Limit the BRDF albedo to a maximum value to account for the energy loss at each bounce.
    const float kMaxAlbedo = 0.9f;
    
    // Store the final ray radiance and hit distance
    radiance += Diffuse_Lambert(min(rayHitGBufferParams.m_Albedo.rgb, float3(kMaxAlbedo, kMaxAlbedo, kMaxAlbedo))) * irradiance;
    DDGIStoreProbeRayFrontfaceHit(g_OutRayData, outputCoords, volume, saturate(radiance), radianceRayQuery.CommittedRayT());
}
