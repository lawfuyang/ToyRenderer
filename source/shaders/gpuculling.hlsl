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
RWStructuredBuffer<uint> g_InstanceVisibilityBuffer : register(u4);
SamplerState g_LinearClampMinReductionSampler : register(s0);

// occlusion culling with AABB is completely busted when an object is extremely near the camera
// also, not exactly accurate for overly large objects.
#define CULL_WITH_BS 1

// Niagara's frustum culling
bool FrustumCullBS(float3 sphereCenterViewSpace, float radius)
{
    bool visible = true;
    
	// the left/top/right/bottom plane culling utilizes frustum symmetry to cull against two planes at the same time
    visible &= sphereCenterViewSpace.z * g_GPUCullingPassConstants.m_Frustum.y + abs(sphereCenterViewSpace.x) * g_GPUCullingPassConstants.m_Frustum.x < radius;
    visible &= sphereCenterViewSpace.z * g_GPUCullingPassConstants.m_Frustum.w + abs(sphereCenterViewSpace.y) * g_GPUCullingPassConstants.m_Frustum.z < radius;
    
	// the near plane culling uses camera space Z directly
    // NOTE: this seems unnecessary?
    //visible &= (sphereCenterViewSpace.z - radius) < g_GPUCullingPassConstants.m_NearPlane;
    
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

bool FrustumCullAABB(float3 aabbCenter, float3 aabbExtents, out float3 clipSpaceAABBCorners[8])
{
    float3 ext = 2.0f * aabbExtents;
    float4x4 extentsBasis = float4x4(
        ext.x, 0, 0, 0,
            0, ext.y, 0, 0,
            0, 0, ext.z, 0,
            0, 0, 0, 0
        );

    float3x4 axis;
    axis[0] = mul(float4(aabbExtents.x * 2, 0, 0, 0), g_GPUCullingPassConstants.m_ViewProjMatrix);
    axis[1] = mul(float4(0, aabbExtents.y * 2, 0, 0), g_GPUCullingPassConstants.m_ViewProjMatrix);
    axis[2] = mul(float4(0, 0, aabbExtents.z * 2, 0), g_GPUCullingPassConstants.m_ViewProjMatrix);

    float4 pos000 = mul(float4(aabbCenter - aabbExtents, 1), g_GPUCullingPassConstants.m_ViewProjMatrix);
    float4 pos100 = pos000 + axis[0];
    float4 pos010 = pos000 + axis[1];
    float4 pos110 = pos010 + axis[0];
    float4 pos001 = pos000 + axis[2];
    float4 pos101 = pos100 + axis[2];
    float4 pos011 = pos010 + axis[2];
    float4 pos111 = pos110 + axis[2];
    
    // Plane inequalities
    float4 planeMins = Min3(
                        Min3(float4(pos000.xy, -pos000.xy) - pos000.w, float4(pos001.xy, -pos001.xy) - pos001.w, float4(pos010.xy, -pos010.xy) - pos010.w),
                        Min3(float4(pos100.xy, -pos100.xy) - pos100.w, float4(pos110.xy, -pos110.xy) - pos110.w, float4(pos011.xy, -pos011.xy) - pos011.w),
                        Min3(float4(pos101.xy, -pos101.xy) - pos101.w, float4(pos111.xy, -pos111.xy) - pos111.w, float4(1, 1, 1, 1))
                        );

    // Clip space AABB
    clipSpaceAABBCorners[0] = pos000.xyz / pos000.w;
    clipSpaceAABBCorners[1] = pos100.xyz / pos100.w;
    clipSpaceAABBCorners[2] = pos010.xyz / pos010.w;
    clipSpaceAABBCorners[3] = pos110.xyz / pos110.w;
    clipSpaceAABBCorners[4] = pos001.xyz / pos001.w;
    clipSpaceAABBCorners[5] = pos101.xyz / pos101.w;
    clipSpaceAABBCorners[6] = pos011.xyz / pos011.w;
    clipSpaceAABBCorners[7] = pos111.xyz / pos111.w;

    float rectMinZ = Min3(
                     Min3(clipSpaceAABBCorners[0].z, clipSpaceAABBCorners[1].z, clipSpaceAABBCorners[2].z),
                     Min3(clipSpaceAABBCorners[3].z, clipSpaceAABBCorners[4].z, clipSpaceAABBCorners[5].z),
                     Min3(clipSpaceAABBCorners[6].z, clipSpaceAABBCorners[7].z, 1.0f)
                     );

    // in front of near plane
    bool bIsVisible = rectMinZ <= 1.0f;

    // within left/right/top/bottom plane
    bIsVisible &= !any(planeMins > 0.0f);

    return bIsVisible;
}

// https://blog.selfshadow.com/publications/practical-visibility/, modified for inversed depth buffer
bool OcclusionCullAABB(float3 clipSpaceAABBCorners[8])
{
    float nearestZ = 0.0f;
    float2 minUV = float2(1.0f, 1.0f);
    float2 maxUV = float2(0.0f, 0.0f);
 
    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float3 clipPos = clipSpaceAABBCorners[i];
        clipPos.xy = ClipXYToUV(clamp(clipPos.xy, -1, 1));
 
        minUV = min(minUV, clipPos.xy);
        maxUV = max(maxUV, clipPos.xy);
        nearestZ = saturate(max(nearestZ, clipPos.z));
    }
 
    float4 boxUVs = float4(minUV, maxUV);
 
    // Calculate hi-Z buffer mip
    int2 size = (maxUV - minUV) * g_GPUCullingPassConstants.m_HZBDimensions;
    float mip = ceil(log2(max(size.x, size.y)));
    
    const float kLowestHZBMip = 3.0f;
    
    // don't bother sampling too high of a mip. 8x8 pixel blockers are fine-grained enough
    mip = max(mip, kLowestHZBMip);
    
    // Texel footprint for the lower (finer-grained) level
    if (mip > kLowestHZBMip)
    {
        float level_lower = mip - 1;
        float2 scale = exp2(-level_lower);
        float2 a = floor(boxUVs.xy * scale);
        float2 b = ceil(boxUVs.zw * scale);
        float2 dims = b - a;
 
        // Use the lower level if we only touch <= 2 texels in both dimensions
        if (dims.x <= 2 && dims.y <= 2)
            mip = level_lower;
    }
    
    //load depths from high z buffer
    float furthestDepth = g_HZB.SampleLevel(g_LinearClampMinReductionSampler, (minUV + maxUV) * 0.5f, mip).x;
 
    return (nearestZ >= furthestDepth);
}

[numthreads(kNbGPUCullingGroupThreads, 1, 1)]
void CS_GPUCulling(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint nbInstances = g_GPUCullingPassConstants.m_NbInstances;
    if (dispatchThreadID.x >= nbInstances)
    {
        return;
    }
    
    const bool bDoOcclusionCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_OcclusionCullingEnable;
    const bool bDoFrustumCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_FrustumCullingEnable;
    
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];
    
#if !LATE
    if (!bDoOcclusionCulling)
    {
        return;
    }
    
    // in early pass, we have to *only* render clusters that were visible last frame, to build a reasonable depth pyramid out of visible triangles
    if (bDoOcclusionCulling && ((g_InstanceVisibilityBuffer[instanceConstsIdx] & InstanceVisibilityFlag_VisibleLastFrame) == 0))
    {
        return;
    }
#endif // !LATE
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    bool bIsVisible = true;
    
#if CULL_WITH_BS
    float3 sphereCenterViewSpace = mul(float4(instanceConsts.m_BoundingSphere.xyz, 1.0f), g_GPUCullingPassConstants.m_ViewMatrix).xyz;
    sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
    bool bFrustumCullPassed = FrustumCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
#else
    float3 clipSpaceAABBCorners[8];
    bool bFrustumCullPassed = FrustumCullAABB(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, clipSpaceAABBCorners);
#endif
    
    if (bDoFrustumCulling)
    {
        bIsVisible = bFrustumCullPassed;
    }
    
#if LATE
    if (bIsVisible && bDoOcclusionCulling)
    {
    #if CULL_WITH_BS
        bIsVisible = OcclusionCullBS(sphereCenterViewSpace, instanceConsts.m_BoundingSphere.w);
    #else
        bIsVisible = OcclusionCullAABB(clipSpaceAABBCorners);
    #endif
    }
    
    if (bDoOcclusionCulling)
    {
        if (bIsVisible)
        {
            InterlockedOr(g_InstanceVisibilityBuffer[instanceConstsIdx], InstanceVisibilityFlag_VisibleLastFrame);
        }
        else
        {
            InterlockedAnd(g_InstanceVisibilityBuffer[instanceConstsIdx], ~InstanceVisibilityFlag_VisibleLastFrame);
        }
        
        // in late pass, we have to process objects visible last frame again (after rendering them in early pass)
        // in early pass, we render previously visible clusters
        // in late pass, we must invert the test to *not* render previously visible clusters of previously visible objects because they were rendered in early pass.
        if (g_InstanceVisibilityBuffer[instanceConstsIdx] & InstanceVisibilityFlag_VisibleThisFrame)
        {
            bIsVisible = false;
        }
    }
#else
    if (bDoOcclusionCulling)
    {
        InterlockedOr(g_InstanceVisibilityBuffer[instanceConstsIdx], InstanceVisibilityFlag_VisibleThisFrame);
    }
#endif // LATE
    
    if (!bIsVisible)
    {
        return;
    }
    
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
