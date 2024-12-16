#include "common.hlsli"

#include "shared/MeshData.h"
#include "shared/BasePassStructs.h"
#include "shared/GPUCullingStructs.h"
#include "shared/IndirectArguments.h"

#if __INTELLISENSE__
#define LATE 1
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
RWStructuredBuffer<uint> g_LateCullInstanceIndicesCounter : register(u5);
SamplerState g_LinearClampMinReductionSampler : register(s0);

// Niagara's frustum culling
bool FrustumCullBS(float3 sphereCenterViewSpace, float radius)
{
    bool visible = true;
    
	// the left/top/right/bottom plane culling utilizes frustum symmetry to cull against two planes at the same time
    visible &= sphereCenterViewSpace.z * g_GPUCullingPassConstants.m_Frustum.y + abs(sphereCenterViewSpace.x) * g_GPUCullingPassConstants.m_Frustum.x < radius;
    visible &= sphereCenterViewSpace.z * g_GPUCullingPassConstants.m_Frustum.w + abs(sphereCenterViewSpace.y) * g_GPUCullingPassConstants.m_Frustum.z < radius;
    
	// the near plane culling uses camera space Z directly
    // NOTE: this seems unnecessary?
#if 0
    visible &= (sphereCenterViewSpace.z - radius) < g_GPUCullingPassConstants.m_NearPlane;
#endif
    
    return visible;
}

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool OcclusionCullBS(float3 sphereCenterViewSpace, float radius)
{
    // NOTE: this seems unnecessary?
#if 0
    if (c.z - r < g_GPUCullingPassConstants.m_NearPlane)
        return false;
#endif
    
    float3 c = sphereCenterViewSpace;
    
    // trivially accept of sphere intersects camera near plane
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
    
    // clip space -> uv space
    aabb.xy = ClipXYToUV(aabb.xy);
    aabb.zw = ClipXYToUV(aabb.zw);
    
    float width = (aabb.z - aabb.x) * g_GPUCullingPassConstants.m_HZBDimensions.x;
    float height = (aabb.w - aabb.y) * g_GPUCullingPassConstants.m_HZBDimensions.y;

    // Because we only consider 2x2 pixels, we need to make sure we are sampling from a mip that reduces the rectangle to 1x1 texel or smaller.
    // Due to the rectangle being arbitrarily offset, a 1x1 rectangle may cover 2x2 texel area. Using floor() here would require sampling 4 corners
    // of AABB (using bilinear fetch), which is a little slower.
    float level = ceil(log2(max(width, height)));
    
    // don't bother sampling too high of a mip. 8x8 pixel blockers are fine-grained enough
    level = max(level, 3.0f);

    // Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
    float depth = g_HZB.SampleLevel(g_LinearClampMinReductionSampler, (aabb.xy + aabb.zw) * 0.5f, level).x;
    float depthSphere = g_GPUCullingPassConstants.m_NearPlane / (c.z - r);

    return depthSphere > depth;
}

[numthreads(kNbGPUCullingGroupThreads, 1, 1)]
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
    
    const bool bDoOcclusionCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_OcclusionCullingEnable;
    const bool bDoFrustumCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_FrustumCullingEnable;
    
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    // in early pass, we have to *only* render clusters that were visible last frame, to build a reasonable depth pyramid out of visible triangles
    // we do this by culling against the previous frame's view frustum & HZB
    
    float3 sphereCenterViewSpace = mul(float4(instanceConsts.m_BoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_ViewMatrix).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    bool bIsVisible = true;
    
    // Frustum test instance against the current view
    if (bDoFrustumCulling)
    {
        bIsVisible = FrustumCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
    }
    
    if (bIsVisible && bDoOcclusionCulling)
    {
    #if !LATE
        // Frustum test instance against the *previous* view to determine if it was visible last frame
        sphereCenterViewSpace = mul(float4(instanceConsts.m_BoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_PrevViewMatrix).xyz;
        sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
        
        if (bDoFrustumCulling)
        {
            bIsVisible = FrustumCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
        }
        
        // Occlusion test instance against *previous* HZB
        if (bIsVisible)
        {
            bIsVisible = OcclusionCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
        }
    #else
        // Occlusion test instance against the updated HZB
        bIsVisible = OcclusionCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
    #endif
    }
    
    if (!bIsVisible)
    {
        return;
    }
    
#if LATE
    InterlockedAdd(g_CullingCounters[kCullingLateBufferCounterIdx], 1);
#else
    InterlockedAdd(g_LateCullInstanceIndicesCounter[0], 1);
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

StructuredBuffer<int> g_NumLateCullInstances : register(t0);
RWStructuredBuffer<DispatchIndirectArguments> g_LateCullDispatchIndirectArgs : register(u0);

[numthreads(1, 1, 1)]
void CS_BuildLateCullIndirectArgs(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountX = DivideAndRoundUp(g_NumLateCullInstances[0], kNbGPUCullingGroupThreads);
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountY = 1;
    g_LateCullDispatchIndirectArgs[0].m_ThreadGroupCountZ = 1;
}
