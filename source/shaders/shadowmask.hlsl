#include "extern/nvidia/NRD/Shaders/Include/NRD.hlsli"

#include "toyrenderer_common.hlsli"
#include "lightingcommon.hlsli"
#include "raytracingcommon.hlsli"

#include "ShaderInterop.h"

cbuffer g_PassConstantsBuffer : register(b0) { ShadowMaskConsts g_ShadowMaskConsts; }
cbuffer g_ShadowMaskResourceIndicesBuffer : register(b1) { ShadowMaskResourceIndices g_ShadowMaskResourceIndices; }

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
    
    Texture2D                                   depthBuffer                = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_DepthBufferIdx];
    RaytracingAccelerationStructure             sceneTLAS                  = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_SceneTLASIdx];
    Texture2D<uint4>                            GBufferA                   = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_GBufferAIdx];
    StructuredBuffer<BasePassInstanceConstants> basePassInstanceConsts     = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_BasePassInstanceConstsIdx];
    StructuredBuffer<RawVertexFormat>           globalVertexBuffer         = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_GlobalVertexBufferIdx];
    StructuredBuffer<MaterialData>              materialDataBuffer         = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_MaterialDataBufferIdx];
    StructuredBuffer<uint>                      globalIndexIDsBuffer       = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_GlobalIndexIDsBufferIdx];
    StructuredBuffer<MeshData>                  meshDataBuffer             = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_MeshDataBufferIdx];
    Texture2D                                   blueNoise                  = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_BlueNoiseIdx];
    RWTexture2D<float>                          shadowDataOutput           = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_ShadowDataOutputIdx];
    RWTexture2D<float>                          linearViewDepthOutput      = ResourceDescriptorHeap[g_ShadowMaskResourceIndices.m_LinearViewDepthOutputIdx];
    
    sampler anisotropicClampSampler = SamplerDescriptorHeap[g_ShadowMaskResourceIndices.m_SamplersIdx + 0];
    sampler anisotropicWrapSampler = SamplerDescriptorHeap[g_ShadowMaskResourceIndices.m_SamplersIdx + 1];
    sampler anisotropicBorderSampler = SamplerDescriptorHeap[g_ShadowMaskResourceIndices.m_SamplersIdx + 2];
    sampler anisotropicMirrorSampler = SamplerDescriptorHeap[g_ShadowMaskResourceIndices.m_SamplersIdx + 3];
    sampler samplers[SamplerIdx_Count] = { anisotropicClampSampler, anisotropicWrapSampler, anisotropicBorderSampler, anisotropicMirrorSampler };
    
    float depth = depthBuffer[dispatchThreadID.xy].x;
    if (depth == kFarDepth)
    {
        linearViewDepthOutput[dispatchThreadID.xy] = kFP16Max;
        return;
    }
    
    GBufferParams gbufferParams;
    UnpackGBuffer(GBufferA[dispatchThreadID.xy], gbufferParams);
    
    float2 screenUV = (dispatchThreadID.xy + float2(0.5f, 0.5f)) / g_ShadowMaskConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_ShadowMaskConsts.m_ClipToWorld);
    
    // empirical offset to remove shadow acne
    float3 rayOriginOffset = gbufferParams.m_Normal * g_ShadowMaskConsts.m_RayStartOffset;
    
    const float2 noise = blueNoise[dispatchThreadID.xy % 128].rg + g_ShadowMaskConsts.m_NoisePhase;
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
    rayQuery.TraceRayInline(sceneTLAS, RAY_FLAG_NONE, 0xFF, rayDesc);
    
    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            GetRayHitInstanceGBufferParamsArguments args;
            args.m_InstanceID = rayQuery.CommittedInstanceID();
            args.m_PrimitiveIndex = rayQuery.CommittedPrimitiveIndex();
            args.m_AttribBarycentrics = rayQuery.CommittedTriangleBarycentrics();
            args.m_ObjectToWorld3x4 = rayQuery.CommittedObjectToWorld3x4();
            args.m_BasePassInstanceConstantsBuffer = basePassInstanceConsts;
            args.m_MaterialDataBuffer = materialDataBuffer;
            args.m_MeshDataBuffer = meshDataBuffer;
            args.m_GlobalIndexIDsBuffer = globalIndexIDsBuffer;
            args.m_GlobalVertexBuffer = globalVertexBuffer;
            args.m_Samplers = samplers;
            
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
        shadowDataOutput[dispatchThreadID.xy] = SIGMA_FrontEnd_PackPenumbra(bPixelOccluded ? rayQuery.CommittedRayT() : kFP16Max, g_ShadowMaskConsts.m_TanSunAngularRadius);
    }
    else
    {
        shadowDataOutput[dispatchThreadID.xy] = bPixelOccluded ? 0.0f : 1.0f;
    }
    
    linearViewDepthOutput[dispatchThreadID.xy] = length(worldPosition - g_ShadowMaskConsts.m_CameraPosition);
}

cbuffer g_PackNormalAndRoughnessPassConstantsBuffer : register(b0) { PackNormalAndRoughnessConsts g_PackNormalAndRoughnessConsts; }

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
    
    Texture2D<uint4> gGBufferA = ResourceDescriptorHeap[g_PackNormalAndRoughnessConsts.m_GBufferAIdx];
    RWTexture2D<float4> normalRoughnessOutput = ResourceDescriptorHeap[g_PackNormalAndRoughnessConsts.m_NormalRoughnessOutputIdx];
    
    GBufferParams gbufferParams;
    UnpackGBuffer(gGBufferA[dispatchThreadID.xy], gbufferParams);
    
    float4 packed = NRD_FrontEnd_PackNormalAndRoughness(gbufferParams.m_Normal, gbufferParams.m_Roughness, 0);
    normalRoughnessOutput[dispatchThreadID.xy] = packed;
}
