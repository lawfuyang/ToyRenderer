#include "common.hlsli"

#include "hzb.hlsli"

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
SamplerState g_PointClampSampler : register(s0);

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool ProjectSphere(float3 c, float r, float znear, float P00, float P11, out float4 aabb)
{
    if (c.z < r + znear)
        return false;

    float3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    aabb = aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) + float4(0.5f, 0.5f, 0.5f, 0.5f); // clip space -> uv space

    return true;
}

bool ScreenSpaceFrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 worldToClip)
{
    float3 ext = 2.0f * aabbExtents;
    float4x4 extentsBasis = float4x4(
        ext.x, 0, 0, 0,
            0, ext.y, 0, 0,
            0, 0, ext.z, 0,
            0, 0, 0, 0
        );

    float3x4 axis;
    axis[0] = mul(float4(aabbExtents.x * 2, 0, 0, 0), worldToClip);
    axis[1] = mul(float4(0, aabbExtents.y * 2, 0, 0), worldToClip);
    axis[2] = mul(float4(0, 0, aabbExtents.z * 2, 0), worldToClip);

    float4 pos000 = mul(float4(aabbCenter - aabbExtents, 1), worldToClip);
    float4 pos100 = pos000 + axis[0];
    float4 pos010 = pos000 + axis[1];
    float4 pos110 = pos010 + axis[0];
    float4 pos001 = pos000 + axis[2];
    float4 pos101 = pos100 + axis[2];
    float4 pos011 = pos010 + axis[2];
    float4 pos111 = pos110 + axis[2];

    float minW = Min3(Min3(pos000.w, pos100.w, pos010.w),
                      Min3(pos110.w, pos001.w, pos101.w),
                      min(pos011.w, pos111.w));

    float maxW = Max3(Max3(pos000.w, pos100.w, pos010.w),
                      Max3(pos110.w, pos001.w, pos101.w),
                      max(pos011.w, pos111.w));

    // Plane inequalities
    float4 planeMins = Min3(
                        Min3(float4(pos000.xy, -pos000.xy) - pos000.w, float4(pos001.xy, -pos001.xy) - pos001.w, float4(pos010.xy, -pos010.xy) - pos010.w),
                        Min3(float4(pos100.xy, -pos100.xy) - pos100.w, float4(pos110.xy, -pos110.xy) - pos110.w, float4(pos011.xy, -pos011.xy) - pos011.w),
                        Min3(float4(pos101.xy, -pos101.xy) - pos101.w, float4(pos111.xy, -pos111.xy) - pos111.w, float4(1, 1, 1, 1))
                        );

    // Clip space AABB
    float3 csPos000 = pos000.xyz / pos000.w;
    float3 csPos100 = pos100.xyz / pos100.w;
    float3 csPos010 = pos010.xyz / pos010.w;
    float3 csPos110 = pos110.xyz / pos110.w;
    float3 csPos001 = pos001.xyz / pos001.w;
    float3 csPos101 = pos101.xyz / pos101.w;
    float3 csPos011 = pos011.xyz / pos011.w;
    float3 csPos111 = pos111.xyz / pos111.w;

    float3 rectMax = Max3(
                    Max3(csPos000, csPos100, csPos010),
                    Max3(csPos110, csPos001, csPos101),
                    Max3(csPos011, csPos111, float3(-1, -1, -1))
                    );

    bool bIsVisible = rectMax.z > 0;

    if (minW <= 0 && maxW > 0)
    {
        bIsVisible = true;
    }
    else
    {
        bIsVisible &= maxW > 0.0f;
    }

    bIsVisible &= !any(planeMins > 0.0f);

    return bIsVisible;
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
    
    bool bDoOcclusionCulling = g_GPUCullingPassConstants.m_Flags & CullingFlag_OcclusionCullingEnable;
    
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];
    
    // not visible last frame, skip early culling
#if !LATE
    if (bDoOcclusionCulling)
    {
        if (g_InstanceVisibilityBuffer[instanceConstsIdx] == 0)
        {
            return;
        }
    }
#endif // !LATE
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    bool bIsVisible = true;
    
    if (g_GPUCullingPassConstants.m_Flags & CullingFlag_FrustumCullingEnable)
    {
        // cull against outer frustum
        bIsVisible &= ScreenSpaceFrustumCull(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, g_GPUCullingPassConstants.m_WorldToClip);
    }
    
#if LATE
    if (bIsVisible && bDoOcclusionCulling)
    {
        bIsVisible = false;
        
        // todo: pass in as constant?
        const float kZNear = 0.1f;
        
        float3 sphereCenter = instanceConsts.m_BoundingSphere.xyz;
        float sphereRadius = instanceConsts.m_BoundingSphere.w;
        
        float4 aabb;
        if (ProjectSphere(sphereCenter, sphereRadius, kZNear, g_GPUCullingPassConstants.m_Projection00, g_GPUCullingPassConstants.m_Projection11, aabb))
        {
            float width = (aabb.z - aabb.x) * g_GPUCullingPassConstants.m_HZBDimensions.x;
            float height = (aabb.w - aabb.y) * g_GPUCullingPassConstants.m_HZBDimensions.y;

			// Because we only consider 2x2 pixels, we need to make sure we are sampling from a mip that reduces the rectangle to 1x1 texel or smaller.
			// Due to the rectangle being arbitrarily offset, a 1x1 rectangle may cover 2x2 texel area. Using floor() here would require sampling 4 corners
			// of AABB (using bilinear fetch), which is a little slower.
            float level = ceil(log2(max(width, height)));

			// Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
            float depth = g_HZB.SampleLevel(g_PointClampSampler, (aabb.xy + aabb.zw) * 0.5f, level).x;
            float depthSphere = kZNear / (sphereCenter.z - sphereRadius);

            bIsVisible = depthSphere > depth;
        }
    }
#endif // LATE
    
    if (!bIsVisible)
    {
        return;
    }
    
#if LATE
    g_InstanceVisibilityBuffer[instanceConstsIdx] = 1;
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
