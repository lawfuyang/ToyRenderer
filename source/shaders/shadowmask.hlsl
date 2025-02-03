#include "common.hlsli"
#include "lightingcommon.hlsli"

#include "shared/ShadowMaskStructs.h"
#include "shared/BasePassStructs.h"
#include "shared/MaterialData.h"
#include "shared/MeshData.h"
#include "shared/RawVertexFormat.h"

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
Texture2D g_Textures[] : register(t0, space1);
RWTexture2D<float> g_ShadowMaskOutput : register(u0);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror

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

bool IsPixelOccluded(uint instanceID, uint primitiveIndex, float2 attribBarycentrics)
{
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceID];
    MaterialData materialData = g_MaterialDataBuffer[instanceConsts.m_MaterialDataIdx];
        
    if (materialData.m_AlphaCutoff == 0.0f)
    {
        // it's an opaque material with no alpha mask - accept the hit and end the search
        return true;
    }
    
    float alpha = materialData.m_ConstAlbedo.a;
    if (materialData.m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        // TODO: pick appropriate mesh LOD?
        MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
        
        uint indices[3] =
        {
            g_GlobalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 0],
            g_GlobalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 1],
            g_GlobalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 2],
        };
        
        float2 vertexUVs[3] =
        {
            g_GlobalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[0]].m_TexCoord,
            g_GlobalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[1]].m_TexCoord,
            g_GlobalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[2]].m_TexCoord,
        };
        
        float barycentrics[3] = { (1.0f - attribBarycentrics.x - attribBarycentrics.y), attribBarycentrics.x, attribBarycentrics.y };
        float2 finalUV = vertexUVs[0] * barycentrics[0] + vertexUVs[1] * barycentrics[1] + vertexUVs[2] * barycentrics[2];
        
        uint texIdx = NonUniformResourceIndex(materialData.m_AlbedoTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_AlbedoTextureSamplerAndDescriptorIndex >> 30;
        
        Texture2D albedoTexture = g_Textures[texIdx];
        
        // TODO: use 'SampleGrad'
        float textureAlpha = albedoTexture.SampleLevel(g_Samplers[samplerIdx], finalUV, 0).a;
        
        alpha *= textureAlpha;
    }
    
    return alpha >= materialData.m_AlphaCutoff;
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
        return;
    }
    
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[dispatchThreadID.xy], gbufferParams);
    
    float2 screenUV = (dispatchThreadID.xy + float2(0.5f, 0.5f)) / g_ShadowMaskConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_ShadowMaskConsts.m_ClipToWorld);
    
    // empirical offset to remove shadow acne
    float3 rayOriginOffset = gbufferParams.m_Normal * 0.01f;
    
    const float2 noise = g_BlueNoise[dispatchThreadID.xy % 128].rg + g_ShadowMaskConsts.m_NoisePhase;
    float3 rayDirection = normalize(MapToCone(fmod(noise, 1), g_ShadowMaskConsts.m_DirectionalLightDirection, g_ShadowMaskConsts.m_SunSize));
    
    RayDesc rayDesc;
    rayDesc.Origin = worldPosition + rayOriginOffset;
    rayDesc.Direction = rayDirection;
    rayDesc.TMin = 0.1f;
    rayDesc.TMax = kKindaBigNumber;
    
    const uint kFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    
    RayQuery<kFlags> rayQuery;
    rayQuery.TraceRayInline(g_SceneTLAS, kFlags, 0xFF, rayDesc);
    
    while (rayQuery.Proceed())
    {
        if (IsPixelOccluded(rayQuery.CandidateInstanceID(), rayQuery.CandidatePrimitiveIndex(), rayQuery.CandidateTriangleBarycentrics()))
        {
            rayQuery.CommitNonOpaqueTriangleHit();
        }
    }

    bool bPixelOccluded = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    g_ShadowMaskOutput[dispatchThreadID.xy] = bPixelOccluded ? 0.0f : 1.0f;
}
