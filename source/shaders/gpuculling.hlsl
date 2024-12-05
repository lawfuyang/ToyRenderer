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
RWStructuredBuffer<uint> g_InstanceLateVisibilityBuffer : register(u5);
SamplerState g_LinearClampMinReductionSampler : register(s0);

static const float kNearPlane = 0.1f;

bool FrustumCullAABB(float3 aabbCenter, float3 aabbExtents, out float3 clipSpaceAABB[8])
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
    
    clipSpaceAABB[0] = csPos000;
    clipSpaceAABB[1] = csPos100;
    clipSpaceAABB[2] = csPos010;
    clipSpaceAABB[3] = csPos110;
    clipSpaceAABB[4] = csPos001;
    clipSpaceAABB[5] = csPos101;
    clipSpaceAABB[6] = csPos011;
    clipSpaceAABB[7] = csPos111;

    return bIsVisible;
}

// https://blog.selfshadow.com/publications/practical-visibility/, modified for inversed depth buffer
bool OcclusionCullAABB(float3 clipSpaceAABB[8])
{
    float nearestZ = 0.0f;
    float2 minUV = float2(1.0f, 1.0f);
    float2 maxUV = float2(0.0f, 0.0f);
 
    [unroll]
    for (int i = 0; i < 8; i++)
    {
        // transform world space aaBox to NDC
        float3 clipPos = clipSpaceAABB[i];
        clipPos.xy = ClipXYToUV(clipPos.xy);
 
        minUV = min(minUV, clipPos.xy);
        maxUV = max(maxUV, clipPos.xy);
        nearestZ = max(nearestZ, clipPos.z);
    }
 
    float4 boxUVs = float4(minUV, maxUV);
 
    // Calculate hi-Z buffer mip
    int2 size = (maxUV - minUV) * g_GPUCullingPassConstants.m_HZBDimensions;
    float mip = ceil(log2(max(size.x, size.y)));
    
    // Texel footprint for the lower (finer-grained) level
    float level_lower = max(mip - 1, 0);
    float2 scale = exp2(-level_lower);
    float2 a = floor(boxUVs.xy * scale);
    float2 b = ceil(boxUVs.zw * scale);
    float2 dims = b - a;
 
    // Use the lower level if we only touch <= 2 texels in both dimensions
    if (dims.x <= 2 && dims.y <= 2)
        mip = level_lower;
 
    //load depths from high z buffer
    float furthestDepth = g_HZB.SampleLevel(g_LinearClampMinReductionSampler, (boxUVs.xy + boxUVs.zw) * 0.5f, mip).x;
 
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
    if (bDoOcclusionCulling && g_InstanceVisibilityBuffer[instanceConstsIdx] == 0)
    {
        return;
    }
#endif // !LATE
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    bool bIsVisible = true;
    
    float3 clipSpaceAABB[8];
    if (bDoFrustumCulling)
    {
        bIsVisible = FrustumCullAABB(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, clipSpaceAABB);
        //bIsVisible = FrustumCullAABB(instanceConsts.m_BoundingSphere.xyz, instanceConsts.m_BoundingSphere.www);
    }
    
#if LATE
    if (bIsVisible && bDoOcclusionCulling)
    {
        bIsVisible = OcclusionCullAABB(clipSpaceAABB);
    }
    
    if (bDoOcclusionCulling)
    {
        g_InstanceVisibilityBuffer[instanceConstsIdx] = bIsVisible;
        
        // in late pass, we have to process objects visible last frame again (after rendering them in early pass)
        // in early pass, we render previously visible clusters
        // in late pass, we must invert the test to *not* render previously visible clusters of previously visible objects because they were rendered in early pass.
        if (g_InstanceLateVisibilityBuffer[instanceConstsIdx] == 1)
        {
            bIsVisible = false;
        }
    }
#else
    if (bDoOcclusionCulling)
    {
        g_InstanceLateVisibilityBuffer[instanceConstsIdx] = bIsVisible;
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
