#include "common.hlsli"

#include "hzb.hlsli"

#include "shared/MeshData.h"
#include "shared/BasePassStructs.h"
#include "shared/GPUCullingStructs.h"
#include "shared/IndirectArguments.h"

cbuffer g_GPUCullingPassConstantsBuffer : register(b0) { GPUCullingPassConstants g_GPUCullingPassConstants; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<uint> g_PrimitiveIndices : register(t1);
StructuredBuffer<MeshData> g_MeshData : register(t2);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_DrawArgumentsOutput : register(u0);
RWStructuredBuffer<uint> g_StartInstanceConstsOffsets : register(u1);
RWStructuredBuffer<uint> g_InstanceIndexCounter : register(u2);
RWStructuredBuffer<uint> g_CullingCounters : register(u3);
SamplerState g_PointClampSampler : register(s0);

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool ProjectSphere(float3 center, float radius, float znear, float P00, float P11, out float4 aabb)
{
    if (center.z < radius + znear)
        return false;

    float2 cx = -center.xz;
    float2 vx = float2(sqrt(dot(cx, cx) - radius * radius), radius);
    float2 minx = mul(cx, float2x2(vx.x, vx.y, -vx.y, vx.x));
    float2 maxx = mul(cx, float2x2(vx.x, -vx.y, vx.y, vx.x));

    float2 cy = -center.yz;
    float2 vy = float2(sqrt(dot(cy, cy) - radius * radius), radius);
    float2 miny = mul(cy, float2x2(vy.x, vy.y, -vy.y, vy.x));
    float2 maxy = mul(cy, float2x2(vy.x, -vy.y, vy.y, vy.x));

    aabb = float4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
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
    
    uint instanceConstsIdx = g_PrimitiveIndices[dispatchThreadID.x];
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    bool bIsVisible = true;
    
    if (g_GPUCullingPassConstants.m_EnableFrustumCulling)
    {
        bIsVisible &= ScreenSpaceFrustumCull(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, g_GPUCullingPassConstants.m_WorldToClipInclusive);
        
        if (g_GPUCullingPassConstants.m_WorldToClipExclusive._11 != 1.0f)
        {
            bIsVisible &= !ScreenSpaceFrustumCull(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, g_GPUCullingPassConstants.m_WorldToClipExclusive);
        }
        
        if(bIsVisible)
        {
            InterlockedAdd(g_CullingCounters[kFrustumCullingBufferCounterIdx], 1);
        }
    }
    
    if (!bIsVisible)
    {
        return;
    }
    
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

StructuredBuffer<uint> g_NbPhaseTwoInstances : register(t0);
RWStructuredBuffer<DispatchIndirectArguments> g_DispatchIndirectArguments : register(u0);

[numthreads(1, 1, 1)]
void CS_BuildPhaseTwoIndirectArgs()
{
    DispatchIndirectArguments args;
    args.m_ThreadGroupCountX = DivideAndRoundUp(g_NbPhaseTwoInstances[0], kNbGPUCullingGroupThreads);
    args.m_ThreadGroupCountY = 1;
    args.m_ThreadGroupCountZ = 1;
    g_DispatchIndirectArguments[0] = args;
}
