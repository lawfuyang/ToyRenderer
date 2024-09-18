#include "common.hlsli"

#include "hzb.hlsli"

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

	Cull both on a per-instance level as on a per-meshlet level.
	Leverage Mesh/Amplification shaders to drive per-meshlet culling.

	https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
*/

cbuffer g_GPUCullingPassConstantsBuffer : register(b0) { GPUCullingPassConstants g_GPUCullingPassConstants; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<MeshData> g_MeshData : register(t1);
StructuredBuffer<uint> g_VisualProxiesIndices : register(t2);
Texture2D<float> g_HZBTexture : register(t3);
StructuredBuffer<uint> g_SecondPhaseNbInstances : register(t4);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_DrawArgumentsOutput : register(u0);
RWStructuredBuffer<uint> g_StartInstanceConstsOffsets : register(u1);
RWStructuredBuffer<uint> g_InstanceIndexCounter : register(u2);
RWStructuredBuffer<uint> g_CullingCounters : register(u3);
RWStructuredBuffer<uint> g_PhaseTwoVisualProxyIndicesOut : register(u4);
RWStructuredBuffer<uint> g_PhaseTwoVisualProxyIndicesOutCounter : register(u5);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_Phase2DrawArgumentsOutput : register(u6);
RWStructuredBuffer<uint> g_Phase2StartInstanceConstsOffsets : register(u7);
RWStructuredBuffer<uint> g_Phase2InstanceIndexCounter : register(u8);
SamplerState g_PointClampSampler : register(s0);

struct FrustumCullData
{
    bool m_IsVisible;
    float3 m_RectMin;
    float3 m_RectMax;
};

FrustumCullData ScreenSpaceFrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 worldToClip)
{
    FrustumCullData data = (FrustumCullData) 0;
    data.m_IsVisible = true;

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

    data.m_RectMin = Min3(
                    Min3(csPos000, csPos100, csPos010),
                    Min3(csPos110, csPos001, csPos101),
                    Min3(csPos011, csPos111, float3(1, 1, 1))
                    );

    data.m_RectMax = Max3(
                    Max3(csPos000, csPos100, csPos010),
                    Max3(csPos110, csPos001, csPos101),
                    Max3(csPos011, csPos111, float3(-1, -1, -1))
                    );

    data.m_IsVisible &= data.m_RectMax.z > 0;

    if (minW <= 0 && maxW > 0)
    {
        data.m_RectMin = -1;
        data.m_RectMax = 1;
        data.m_IsVisible = true;
    }
    else
    {
        data.m_IsVisible &= maxW > 0.0f;
    }

    data.m_IsVisible &= !any(planeMins > 0.0f);

    return data;
}

[numthreads(kNbGPUCullingGroupThreads, 1, 1)]
void CS_GPUCulling(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const bool bOcclusionCullingEnabled = (g_GPUCullingPassConstants.m_OcclusionCullingFlags & OcclusionCullingFlag_Enable);
    const bool bOcclusionCullingIsFirstPhase = (g_GPUCullingPassConstants.m_OcclusionCullingFlags & OcclusionCullingFlag_IsFirstPhase);
    const bool bOcclusionCullingIsSecondPhase = (bOcclusionCullingEnabled && !bOcclusionCullingIsFirstPhase);
    
    const uint nbInstances = bOcclusionCullingIsSecondPhase ? g_SecondPhaseNbInstances[0] : g_GPUCullingPassConstants.m_NbInstances;
    if (dispatchThreadID.x >= nbInstances)
    {
        return;
    }
    
    uint instanceConstsIdx = bOcclusionCullingIsSecondPhase ? g_VisualProxiesIndices[dispatchThreadID.x] : dispatchThreadID.x;
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstsIdx];
    
    // Frustum test instance against the current view
    FrustumCullData cullData = ScreenSpaceFrustumCull(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, g_GPUCullingPassConstants.m_WorldToClip);
    
    bool bIsVisible = true;
    bool bWasOccluded = false;
    
    if (g_GPUCullingPassConstants.m_EnableFrustumCulling)
    {
        bIsVisible = cullData.m_IsVisible;
        if (bIsVisible)
        {
            InterlockedAdd(g_CullingCounters[kFrustumCullingBufferCounterIdx], 1);
        }
    }
    
    if (bIsVisible && bOcclusionCullingEnabled)
    {
        if (bOcclusionCullingIsFirstPhase)
        {
            // test instance visibiliy against the *previous* view to determine if it was visible last frame
            FrustumCullData prevCullData = ScreenSpaceFrustumCull(instanceConsts.m_AABBCenter, instanceConsts.m_AABBExtents, g_GPUCullingPassConstants.m_PrevFrameWorldToClip);
            if (prevCullData.m_IsVisible)
            {
                // Occlusion test instance against the HZB
                float minDepth = GetMinDepthFromHZB(float4(ClipXYToUV(prevCullData.m_RectMin.xy), ClipXYToUV(prevCullData.m_RectMax.xy)), g_HZBTexture, g_GPUCullingPassConstants.m_HZBDimensions, g_PointClampSampler);
                bWasOccluded = minDepth > prevCullData.m_RectMax.z;
            }
        
            // If the instance was occluded the previous frame, we can't be sure it's still occluded this frame.
            // Add it to the list to re-test in the second phase.
            if (bWasOccluded)
            {
                uint outInstanceIdx;
                InterlockedAdd(g_PhaseTwoVisualProxyIndicesOutCounter[0], 1, outInstanceIdx);
                g_PhaseTwoVisualProxyIndicesOut[outInstanceIdx] = instanceConstsIdx;
            }
        }
        else
        {
            // Occlusion test instance against the updated HZB
            float minDepth = GetMinDepthFromHZB(float4(ClipXYToUV(cullData.m_RectMin.xy), ClipXYToUV(cullData.m_RectMax.xy)), g_HZBTexture, g_GPUCullingPassConstants.m_HZBDimensions, g_PointClampSampler);
            bIsVisible = minDepth <= cullData.m_RectMax.z;
        }
    }

    // TODO
    if (bIsVisible && g_GPUCullingPassConstants.m_EnableMeshletConeCulling)
    {
        
    }
    
    if (!bIsVisible || bWasOccluded)
    {
        return;
    }
    
    // culling passed... add instance to indirect args
    
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
    
    if (bOcclusionCullingEnabled)
    {
        uint counterIdx = bOcclusionCullingIsFirstPhase ? kOcclusionCullingPhase1BufferCounterIdx : kOcclusionCullingPhase2BufferCounterIdx;
        InterlockedAdd(g_CullingCounters[counterIdx], 1);
    }
    
    // output indirect args specifically for phase 2 depth pre-pass
    if (bOcclusionCullingIsSecondPhase)
    {
        uint phase2OutInstanceIdx;
        InterlockedAdd(g_Phase2InstanceIndexCounter[0], 1, phase2OutInstanceIdx);
        
        newArgs.m_StartInstanceLocation = phase2OutInstanceIdx;
        
        g_Phase2DrawArgumentsOutput[phase2OutInstanceIdx] = newArgs;
        g_Phase2StartInstanceConstsOffsets[phase2OutInstanceIdx] = instanceConstsIdx;
    }
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
