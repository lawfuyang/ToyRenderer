#include "extern/nvidia/NRD/Shaders/Include/NRD.hlsli"

#include "toyrenderer_common.hlsli"
#include "lightingcommon.hlsli"
#include "raytracingcommon.hlsli"

#include "ShaderInterop.h"

cbuffer g_PassConstantsBuffer : register(b0) { ShadowMaskConsts g_ShadowMaskConsts; }
Texture2D g_DepthBuffer : register(t0);
RaytracingAccelerationStructure g_SceneTLAS : register(t1);
Texture2D<uint4> g_GBufferA : register(t2);
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t3);
StructuredBuffer<RawVertexFormat> g_GlobalVertexBuffer : register(t4);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t5);
StructuredBuffer<uint> g_GlobalIndexIDsBuffer : register(t6);
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t7);
Texture2D g_BlueNoise : register(t8);
RWTexture2D<float> g_ShadowDataOutput : register(u0);
RWTexture2D<float> g_LinearViewDepthOutput : register(u1);
SamplerState g_AnisotropicClampSampler : register(s0);
SamplerState g_AnisotropicWrapSampler : register(s1);

float2x3 CreateTangentVectors(float3 normal)
{
    float3 up = abs(normal.z) < 0.99999f ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);

    float2x3 tangents;

    tangents[0] = normalize(cross(up, normal));
    tangents[1] = cross(normal, tangents[0]);

    return tangents;
}

float3 MapToCone(float2 s, float3 n, float radius)
{
    const float2 offset = 2.0f * s - float2(1.0f, 1.0f);

    if (offset.x == 0.0f && offset.y == 0.0f)
    {
        return n;
    }

    float theta, r;

    if (abs(offset.x) > abs(offset.y))
    {
        r = offset.x;
        theta = M_PI / 4.0f * (offset.y / offset.x);
    }
    else
    {
        r = offset.y;
        theta = (M_PI * 0.5f) * (1.0f - 0.5f * (offset.x / offset.y));
    }

    const float2 uv = float2(radius * r * cos(theta), radius * r * sin(theta));

    const float2x3 tangents = CreateTangentVectors(n);

    return n + uv.x * tangents[0] + uv.y * tangents[1];
}

[numthreads(8, 8, 1)]
void CS_ShadowMask(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    if (any(dispatchThreadID.xy >= g_ShadowMaskConsts.m_OutputResolution))
    {
        return;
    }
    
    float depth = g_DepthBuffer[dispatchThreadID.xy].x;
    if (depth == kFarDepth)
    {
        g_LinearViewDepthOutput[dispatchThreadID.xy] = kFP16Max;
        return;
    }
    
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[dispatchThreadID.xy], gbufferParams);
    
    float2 screenUV = (dispatchThreadID.xy + float2(0.5f, 0.5f)) / g_ShadowMaskConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_ShadowMaskConsts.m_ClipToWorld);
    
    // empirical offset to remove shadow acne
    float3 rayOriginOffset = gbufferParams.m_Normal * g_ShadowMaskConsts.m_RayStartOffset;

    const float2 noise = g_BlueNoise[dispatchThreadID.xy % 128].rg + g_ShadowMaskConsts.m_NoisePhase;
    float3 rayDirection = normalize(MapToCone(fmod(noise, 1), g_ShadowMaskConsts.m_DirectionalLightDirection, g_ShadowMaskConsts.m_TanSunAngularRadius));
    
    RayDesc rayDesc;
    rayDesc.Origin = worldPosition + rayOriginOffset;
    rayDesc.Direction = rayDirection;
    rayDesc.TMin = g_ShadowMaskConsts.m_RayStartOffset;
    rayDesc.TMax = kKindaBigNumber;
    
    // according to Nvidia:
    // ACCEPT_FIRST_HIT_AND_END_SEARCH ray flag can't be used to optimize tracing, because it can lead to wrong potentially very long hit distances from random distant occluders
    // but i don't care for now... i can't see any difference in the output
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
    rayQuery.TraceRayInline(g_SceneTLAS, RAY_FLAG_NONE, 0xFF, rayDesc);
    
    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            GetRayHitInstanceGBufferParamsArguments args;
            args.m_InstanceID = rayQuery.CommittedInstanceID();
            args.m_PrimitiveIndex = rayQuery.CommittedPrimitiveIndex();
            args.m_AttribBarycentrics = rayQuery.CommittedTriangleBarycentrics();
            args.m_ObjectToWorld3x4 = rayQuery.CommittedObjectToWorld3x4();
            args.m_BasePassInstanceConstantsBuffer = g_BasePassInstanceConsts;
            args.m_MaterialDataBuffer = g_MaterialDataBuffer;
            args.m_MeshDataBuffer = g_MeshDataBuffer;
            args.m_GlobalIndexIDsBuffer = g_GlobalIndexIDsBuffer;
            args.m_GlobalVertexBuffer = g_GlobalVertexBuffer;
            args.m_AnisotropicWrapSampler = g_AnisotropicWrapSampler;
            args.m_AnisotropicClampSampler = g_AnisotropicClampSampler;
            
            GBufferParams gbufferParams = GetRayHitInstanceGBufferParams(args);
            
            if (gbufferParams.m_Albedo.a >= gbufferParams.m_AlphaCutoff)
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    bool bPixelOccluded = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (g_ShadowMaskConsts.m_bDoDenoising)
    {
        g_ShadowDataOutput[dispatchThreadID.xy] = SIGMA_FrontEnd_PackPenumbra(bPixelOccluded ? rayQuery.CommittedRayT() : kFP16Max, g_ShadowMaskConsts.m_TanSunAngularRadius);
    }
    else
    {
        g_ShadowDataOutput[dispatchThreadID.xy] = bPixelOccluded ? 0.0f : 1.0f;
    }

    g_LinearViewDepthOutput[dispatchThreadID.xy] = length(worldPosition - g_ShadowMaskConsts.m_CameraPosition);
}

cbuffer g_PackNormalAndRoughnessPassConstantsBuffer : register(b0) { PackNormalAndRoughnessConsts g_PackNormalAndRoughnessConsts; }
RWTexture2D<float4> g_NormalRoughnessOutput : register(u0);

[numthreads(8, 8, 1)]
void CS_PackNormalAndRoughness(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    if (any(dispatchThreadID.xy >= g_PackNormalAndRoughnessConsts.m_OutputResolution))
    {
        return;
    }
    
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[dispatchThreadID.xy], gbufferParams);
    
    float4 packed = NRD_FrontEnd_PackNormalAndRoughness(gbufferParams.m_Normal, gbufferParams.m_Roughness, 0);
    g_NormalRoughnessOutput[dispatchThreadID.xy] = packed;
}
