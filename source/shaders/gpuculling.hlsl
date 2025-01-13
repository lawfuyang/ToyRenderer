#include "common.hlsli"

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
#endif

cbuffer g_GPUCullingPassConstantsBuffer : register(b0) { GPUCullingPassConstants g_GPUCullingPassConstants; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<uint> g_PrimitiveIndices : register(t1);
StructuredBuffer<MeshData> g_MeshData : register(t2);
Texture2D g_HZB : register(t3);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_DrawArgumentsOutput : register(u0);
RWStructuredBuffer<uint> g_StartInstanceConstsOffsets : register(u1);
RWStructuredBuffer<uint> g_InstanceIndexCounter : register(u2);
RWStructuredBuffer<uint> g_CullingCounters : register(u3);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesCounter : register(u4);
RWStructuredBuffer<uint> g_LateCullInstanceIndicesBuffer : register(u5);
SamplerState g_LinearClampMinReductionSampler : register(s0);

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool OcclusionCull(float3 sphereCenterViewSpace, float radius)
{
    float3 c = sphereCenterViewSpace;
    
    // trivially accept if sphere intersects camera near plane
    if ((c.z - g_GPUCullingPassConstants.m_NearPlane) < radius)
        return true;
    
    float r = radius;
    float P00 = g_GPUCullingPassConstants.m_P00;
    float P11 = g_GPUCullingPassConstants.m_P11;

    float3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    float4 aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    
    aabb.xy = clamp(aabb.xy, -1, 1);
    aabb.zw = clamp(aabb.zw, -1, 1);
    
    // clip space -> uv space
    aabb.xy = ClipXYToUV(aabb.xy);
    aabb.zw = ClipXYToUV(aabb.zw);
    
    float width = (aabb.z - aabb.x) * g_GPUCullingPassConstants.m_HZBDimensions.x;
    float height = (aabb.w - aabb.y) * g_GPUCullingPassConstants.m_HZBDimensions.y;
    float level = floor(log2(max(width, height)));
    
    // Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
    float depth = g_HZB.SampleLevel(g_LinearClampMinReductionSampler, (aabb.xy + aabb.zw) * 0.5f, level).x;
    float depthSphere = g_GPUCullingPassConstants.m_NearPlane / (c.z - r);

    return depthSphere >= depth;
}

void SubmitInstance(uint instanceConstsIdx, BasePassInstanceConstants instanceConsts)
{
#if LATE
    InterlockedAdd(g_CullingCounters[kCullingLateBufferCounterIdx], 1);
#else
    InterlockedAdd(g_CullingCounters[kCullingEarlyBufferCounterIdx], 1);
#endif
    
    uint outInstanceIdx;
    InterlockedAdd(g_InstanceIndexCounter[0], 1, outInstanceIdx);

    MeshData meshData = g_MeshData[instanceConsts.m_MeshDataIdx];
    
    DrawIndexedIndirectArguments newArgs;
    newArgs.m_IndexCount = meshData.m_IndexCount;
    newArgs.m_InstanceCount = 1;
    newArgs.m_StartIndexLocation = meshData.m_StartIndexLocation;
    newArgs.m_BaseVertexLocation = 0;
    newArgs.m_StartInstanceLocation = outInstanceIdx;
    
    g_DrawArgumentsOutput[outInstanceIdx] = newArgs;
    g_StartInstanceConstsOffsets[outInstanceIdx] = instanceConstsIdx;
}

[numthreads(64, 1, 1)]
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
    if (!OcclusionCull(sphereCenterViewSpace, sphereRadius))
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
    if (OcclusionCull(sphereCenterViewSpace, sphereRadius))
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
