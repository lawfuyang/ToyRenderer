#include "toyrenderer_common.hlsli"
#include "culling.hlsli"

#include "ShaderInterop.h"

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
#define LATE_CULL 0
#endif

cbuffer g_GPUCullingPassConstantsBuffer : register(b0) { GPUCullingPassConstants g_GPUCullingPassConstants; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<uint> g_PrimitiveIndices : register(t1);
StructuredBuffer<MeshData> g_MeshData : register(t2);
Texture2D g_HZB : register(t3);
RWStructuredBuffer<MeshletAmplificationData> g_MeshletAmplificationDataBuffer : register(u0);
RWStructuredBuffer<DispatchIndirectArguments> g_MeshletDispatchArgumentsBuffer : register(u1);
RWStructuredBuffer<uint> g_InstanceIndexCounter : register(u2);
RWStructuredBuffer<uint> g_CullingCounters : register(u3);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesCounter : register(u4);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesBuffer : register(u5);
SamplerState g_LinearClampMinReductionSampler : register(s0);

void SubmitInstance(uint instanceConstsIdx, BasePassInstanceConstants instanceConsts, float4 boundingSphereViewSpace)
{
    MeshData meshData = g_MeshData[instanceConsts.m_MeshDataIdx];
    
    uint meshLOD = 0;
    
    uint forcedMeshLOD = g_GPUCullingPassConstants.m_ForcedMeshLOD;
    if (forcedMeshLOD != kInvalidMeshLOD)
    {
        meshLOD = min(forcedMeshLOD, meshData.m_NumLODs - 1);
    }
    else
    {
        float distance = max(length(boundingSphereViewSpace.xyz) - boundingSphereViewSpace.w, 0.0f);
        float threshold = distance * g_GPUCullingPassConstants.m_MeshLODTarget / GetMaxScaleFromWorldMatrix(instanceConsts.m_WorldMatrix);

        for (uint i = 1; i < meshData.m_NumLODs; ++i)
        {
            if (meshData.m_MeshLODDatas[i].m_Error < threshold)
            {
                meshLOD = i;
            }
        }
    }
    
    MeshLODData meshLODData = meshData.m_MeshLODDatas[meshLOD];
    
    uint numWorkGroups = DivideAndRoundUp(meshLODData.m_NumMeshlets, kNumThreadsPerWave);
    
    uint workGroupOffset;
    InterlockedAdd(g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountX, numWorkGroups, workGroupOffset);
    g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountY = 1;
    g_MeshletDispatchArgumentsBuffer[0].m_ThreadGroupCountZ = 1;
    
    // drop workgroups if we exceed the max number of thread groups
    // set counter to a invalid value to signal that we burst the limit
    if (workGroupOffset + numWorkGroups >= kMaxThreadGroupsPerDimension)
    {
        g_CullingCounters[kCullingEarlyInstancesBufferCounterIdx] = 0xFFFFFFFF;
        return;
    }
    
    for (uint i = 0; i < numWorkGroups; ++i)
    {
        MeshletAmplificationData newData;
        newData.m_InstanceConstIdx = instanceConstsIdx;
        newData.m_MeshLOD = meshLOD;
        newData.m_MeshletGroupOffset = i * kNumThreadsPerWave;
        
        g_MeshletAmplificationDataBuffer[workGroupOffset + i] = newData;
    }
    
#if LATE_CULL
    InterlockedAdd(g_CullingCounters[kCullingLateInstancesBufferCounterIdx], 1);
#else
    InterlockedAdd(g_CullingCounters[kCullingEarlyInstancesBufferCounterIdx], 1);
#endif
}

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_GPUCulling(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{    
#if LATE_CULL
    uint nbInstances = g_LateCullInstanceIndicesCounter[0];
#else
    uint nbInstances = g_GPUCullingPassConstants.m_NbInstances;
#endif
    

    if (dispatchThreadID.x >= nbInstances)
    {
        return;
    }
    
#if LATE_CULL
    uint instanceConstsIdx = g_LateCullInstanceIndicesBuffer[dispatchThreadID.x];
#else
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];
#endif
    
    const bool bDoFrustumCulling = g_GPUCullingPassConstants.m_CullingFlags & kCullingFlagFrustumCullingEnable;
    const bool bDoOcclusionCulling = g_GPUCullingPassConstants.m_CullingFlags & kCullingFlagOcclusionCullingEnable;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    float4 instanceBoundingSphere = TransformBoundingSphereToWorld(instanceConsts.m_WorldMatrix, g_MeshData[instanceConsts.m_MeshDataIdx].m_BoundingSphere);
    
    float3 sphereCenterViewSpace = mul(float4(instanceBoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_WorldToView).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    float sphereRadius = instanceBoundingSphere.w;
    
    // Frustum test instance against the current view
    #if LATE_CULL
        // frustum test already done & passed for early cull pass
        bool bIsVisible = true;
    #else
        bool bIsVisible = !bDoFrustumCulling || FrustumCull(sphereCenterViewSpace, sphereRadius, g_GPUCullingPassConstants.m_Frustum);
    #endif
    
    if (!bIsVisible)
    {
        return;
    }
    
    if (!bDoOcclusionCulling)
    {
        SubmitInstance(instanceConstsIdx, instanceConsts, float4(sphereCenterViewSpace.xyz, sphereRadius));
        return;
    }
    
    // test against prev frame HZB for early cull
#if !LATE_CULL
    sphereCenterViewSpace = mul(float4(instanceBoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_PrevWorldToView).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
#endif
    
    OcclusionCullArguments occlusionCullArguments;
    occlusionCullArguments.m_SphereCenterViewSpace = sphereCenterViewSpace;
    occlusionCullArguments.m_Radius = sphereRadius;
    occlusionCullArguments.m_NearPlane = g_GPUCullingPassConstants.m_NearPlane;
    occlusionCullArguments.m_P00 = g_GPUCullingPassConstants.m_P00;
    occlusionCullArguments.m_P11 = g_GPUCullingPassConstants.m_P11;
    occlusionCullArguments.m_HZB = g_HZB;
    occlusionCullArguments.m_HZBDimensions = g_GPUCullingPassConstants.m_HZBDimensions;
    occlusionCullArguments.m_LinearClampMinReductionSampler = g_LinearClampMinReductionSampler;
    
    bool bOcclusionCullResult = OcclusionCull(occlusionCullArguments);
    
#if !LATE_CULL
    // Occlusion test instance against *previous* HZB. If the instance was occluded the previous frame, re-test in the second phase.
    if (!bOcclusionCullResult)
    {
        uint outLateCullInstanceIdx;
        InterlockedAdd(g_LateCullInstanceIndicesCounter[0], 1, outLateCullInstanceIdx);
        g_LateCullInstanceIndicesBuffer[outLateCullInstanceIdx] = instanceConstsIdx;
    }
    else
    {
        // If instance is visible and wasn't occluded in the previous frame, submit it
        SubmitInstance(instanceConstsIdx, instanceConsts, float4(sphereCenterViewSpace.xyz, sphereRadius));
    }
#else
    // Occlusion test instance against the updated HZB
    if (bOcclusionCullResult)
    {
        SubmitInstance(instanceConstsIdx, instanceConsts,float4(sphereCenterViewSpace.xyz, sphereRadius));
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
