#include "common.hlsli"
#include "culling.hlsli"

#include "shared/MeshData.h"
#include "shared/BasePassStructs.h"
#include "shared/GPUCullingStructs.h"
#include "shared/IndirectArguments.h"

/*
	-- 2 Phase Occlusion Culling --

	Works under the assumption that it's likely that objects visible in the previous frame, will be visible this frame.

	In Phase 1, we render all objects that were visible last frame by testing against the previous HZB.
	Occluded objects are stored in a list, to be processed later.
	The HZB is constructed from the current result.
	Phase 2 tests all previously occluded objects against the new HZB and renders unoccluded.
	The HZB is constructed again from this result to be used in the next frame.

	https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
*/

#if __INTELLISENSE__
#define LATE 0
#define MESHLET 1
#endif

cbuffer g_GPUCullingPassConstantsBuffer : register(b0) { GPUCullingPassConstants g_GPUCullingPassConstants; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<uint> g_PrimitiveIndices : register(t1);
StructuredBuffer<MeshData> g_MeshData : register(t2);
Texture2D g_HZB : register(t3);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_DrawArgumentsOutput : register(u0);
RWStructuredBuffer<MeshletAmplificationData> g_MeshletAmplificationDataBuffer : register(u0);
RWStructuredBuffer<uint> g_StartInstanceConstsOffsets : register(u1);
RWStructuredBuffer<DispatchIndirectArguments> g_MeshletDispatchArgumentsBuffer : register(u1);
RWStructuredBuffer<uint> g_InstanceIndexCounter : register(u2);
RWStructuredBuffer<uint> g_CullingCounters : register(u3);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesCounter : register(u4);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesBuffer : register(u5);
SamplerState g_LinearClampMinReductionSampler : register(s0);

void SubmitInstance(uint instanceConstsIdx, BasePassInstanceConstants instanceConsts)
{
#if LATE
    InterlockedAdd(g_CullingCounters[kCullingLateInstancesBufferCounterIdx], 1);
#else
    InterlockedAdd(g_CullingCounters[kCullingEarlyInstancesBufferCounterIdx], 1);
#endif

    MeshData meshData = g_MeshData[instanceConsts.m_MeshDataIdx];
    
#if MESHLET
    uint numWorkGroups = DivideAndRoundUp(meshData.m_MeshletCount, kNumThreadsPerWave);
    
    uint workGroupOffset;
    InterlockedAdd(g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountX, numWorkGroups, workGroupOffset);
    g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountY = 1;
    g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountZ = 1;
    
    for (uint i = 0; i < numWorkGroups; ++i)
    {
        MeshletAmplificationData newData;
        newData.m_InstanceConstIdx = instanceConstsIdx;
        newData.m_MeshletGroupOffset = i * kNumThreadsPerWave;
        
        g_MeshletAmplificationDataBuffer[workGroupOffset + i] = newData;
    }
#else
    uint outInstanceIdx;
    InterlockedAdd(g_InstanceIndexCounter[0], 1, outInstanceIdx);
    
    DrawIndexedIndirectArguments newArgs;
    newArgs.m_IndexCount = meshData.m_IndexCount;
    newArgs.m_InstanceCount = 1;
    newArgs.m_StartIndexLocation = meshData.m_StartIndexLocation;
    newArgs.m_BaseVertexLocation = 0;
    newArgs.m_StartInstanceLocation = outInstanceIdx;
    
    g_DrawArgumentsOutput[outInstanceIdx] = newArgs;
    g_StartInstanceConstsOffsets[outInstanceIdx] = instanceConstsIdx;
#endif
}

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_GPUCulling(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{    
#if LATE
    uint nbInstances = g_LateCullInstanceIndicesCounter[0];
#else
    uint nbInstances = g_GPUCullingPassConstants.m_NbInstances;
#endif
    
    if (dispatchThreadID.x >= nbInstances)
    {
        return;
    }
    
#if LATE
    uint instanceConstsIdx = g_LateCullInstanceIndicesBuffer[dispatchThreadID.x];
#else
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];
#endif
    
    const bool bDoOcclusionCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_OcclusionCullingEnable;
    const bool bDoFrustumCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_FrustumCullingEnable;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    float3 sphereCenterViewSpace = mul(float4(instanceConsts.m_BoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_ViewMatrix).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    float sphereRadius = instanceConsts.m_BoundingSphere.w;
    
    // Frustum test instance against the current view
    bool bIsVisible = !bDoFrustumCulling || FrustumCull(sphereCenterViewSpace, sphereRadius, g_GPUCullingPassConstants.m_Frustum);
    
    if (!bIsVisible)
    {
        return;
    }
    
    if (!bDoOcclusionCulling)
    {
        SubmitInstance(instanceConstsIdx, instanceConsts);
        return;
    }
    
#if !LATE
    sphereCenterViewSpace = mul(float4(instanceConsts.m_BoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_PrevViewMatrix).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    // Occlusion test instance against *previous* HZB. If the instance was occluded the previous frame, re-test in the second phase.
    if (!OcclusionCull(
            sphereCenterViewSpace,
            sphereRadius,
            g_GPUCullingPassConstants.m_NearPlane,
            g_GPUCullingPassConstants.m_P00,
            g_GPUCullingPassConstants.m_P11,
            g_HZB,
            g_GPUCullingPassConstants.m_HZBDimensions,
            g_LinearClampMinReductionSampler))
    {
        uint outLateCullInstanceIdx;
        InterlockedAdd(g_LateCullInstanceIndicesCounter[0], 1, outLateCullInstanceIdx);
        g_LateCullInstanceIndicesBuffer[outLateCullInstanceIdx] = instanceConstsIdx;
    }
    else
    {
        // If instance is visible and wasn't occluded in the previous frame, submit it
        SubmitInstance(instanceConstsIdx, instanceConsts);
    }
#else
    // Occlusion test instance against the updated HZB
    if (OcclusionCull(
            sphereCenterViewSpace,
            sphereRadius,
            g_GPUCullingPassConstants.m_NearPlane,
            g_GPUCullingPassConstants.m_P00,
            g_GPUCullingPassConstants.m_P11,
            g_HZB,
            g_GPUCullingPassConstants.m_HZBDimensions,
            g_LinearClampMinReductionSampler))
    {
        SubmitInstance(instanceConstsIdx, instanceConsts);
    }
#endif
}

StructuredBuffer<int> g_NumLateCullInstances : register(t0);
RWStructuredBuffer<DispatchIndirectArguments> g_LateCullDispatchIndirectArgs : register(u0);

[numthreads(1, 1, 1)]
void CS_BuildLateCullIndirectArgs(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountX = DivideAndRoundUp(g_NumLateCullInstances[0], 64);
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountY = 1;
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountZ = 1;
}
