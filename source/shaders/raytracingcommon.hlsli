#pragma once

#include "lightingcommon.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

float4 SampleMaterialValue(
    float2 texCoord,
    uint materialFlag,
    MaterialData materialData,
    Texture2D bindlessTextures[],
    sampler samplers[],
    float4 defaultValue)
{
    if (!(materialData.m_MaterialFlags & materialFlag))
    {
        return defaultValue;
    }
    
    uint textureSamplerAndDescriptorIndex;
    switch (materialFlag)
    {
        case MaterialFlag_UseDiffuseTexture:
            textureSamplerAndDescriptorIndex = materialData.m_AlbedoTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseNormalTexture:
            textureSamplerAndDescriptorIndex = materialData.m_NormalTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseMetallicRoughnessTexture:
            textureSamplerAndDescriptorIndex = materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseEmissiveTexture:
            textureSamplerAndDescriptorIndex = materialData.m_EmissiveTextureSamplerAndDescriptorIndex;
            break;
    }
        
    uint texIdx = NonUniformResourceIndex(textureSamplerAndDescriptorIndex & 0x3FFFFFFF);
    uint samplerIdx = textureSamplerAndDescriptorIndex >> 30;
        
    // TODO: use 'SampleGrad' for appropriate mip level
    return bindlessTextures[texIdx].SampleLevel(samplers[samplerIdx], texCoord, 0);
}

template<uint kRayQueryFlags>
GBufferParams GetRayHitInstanceGBufferParams(
    RayQuery<kRayQueryFlags> rayQuery,
    StructuredBuffer<BasePassInstanceConstants> basePassInstanceConstantsBuffer,
    StructuredBuffer<MaterialData> materialDataBuffer,
    StructuredBuffer<MeshData> meshDataBuffer,
    StructuredBuffer<uint> globalIndexIDsBuffer,
    StructuredBuffer<RawVertexFormat> globalVertexBuffer,
    Texture2D bindlessTextures[],
    sampler samplers[],
    out float3 rayHitWorldPosition)
{
    GBufferParams result = (GBufferParams)0;
    
    uint instanceID = rayQuery.CandidateInstanceID();
    uint primitiveIndex = rayQuery.CandidatePrimitiveIndex();
    float2 attribBarycentrics = rayQuery.CandidateTriangleBarycentrics();
    
    BasePassInstanceConstants instanceConsts = basePassInstanceConstantsBuffer[instanceID];
    MaterialData materialData = materialDataBuffer[instanceConsts.m_MaterialDataIdx];
    
    result.m_AlphaCutoff = materialData.m_AlphaCutoff;
    
    // TODO: pick appropriate mesh LOD?
    MeshData meshData = meshDataBuffer[instanceConsts.m_MeshDataIdx];
        
    uint indices[3] =
    {
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 0],
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 1],
        globalIndexIDsBuffer[meshData.m_GlobalIndexBufferIdx + primitiveIndex * 3 + 2],
    };
    
    RawVertexFormat vertices[3] =
    {
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[0]],
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[1]],
        globalVertexBuffer[meshData.m_GlobalVertexBufferIdx + indices[2]],
    };
    
    float barycentrics[3] = { (1.0f - attribBarycentrics.x - attribBarycentrics.y), attribBarycentrics.x, attribBarycentrics.y };
    float2 finalUV = vertices[0].m_TexCoord * barycentrics[0] + vertices[1].m_TexCoord * barycentrics[1] + vertices[2].m_TexCoord * barycentrics[2];
    
    float4 albedoSample = SampleMaterialValue(finalUV, MaterialFlag_UseDiffuseTexture, materialData, bindlessTextures, samplers, float4(1, 1, 1, 1));
    float4 normalSample = SampleMaterialValue(finalUV, MaterialFlag_UseNormalTexture, materialData, bindlessTextures, samplers, float4(0.5f, 0.5f, 1.0f, 0));
    float4 metalRoughnessSample = SampleMaterialValue(finalUV, MaterialFlag_UseMetallicRoughnessTexture, materialData, bindlessTextures, samplers, float4(0.0f, 1.0f, 0.0f, 0));
    float4 emissiveSample = SampleMaterialValue(finalUV, MaterialFlag_UseEmissiveTexture, materialData, bindlessTextures, samplers, float4(1, 1, 1, 0));
   
    result.m_Albedo = materialData.m_ConstAlbedo * albedoSample;
    result.m_Roughness = metalRoughnessSample.g;
    result.m_Metallic = metalRoughnessSample.b;
    result.m_Emissive = materialData.m_ConstEmissive * emissiveSample.rgb;
    
    rayHitWorldPosition =
        vertices[0].m_Position * barycentrics[0] +
        vertices[1].m_Position * barycentrics[1] +
        vertices[2].m_Position * barycentrics[2];
    rayHitWorldPosition = mul(rayQuery.CommittedObjectToWorld3x4(), float4(rayHitWorldPosition, 1.0f)).xyz;
    
    float3 geometricNormal =
        UnpackR10G10B10A2F(vertices[0].m_PackedNormal).xyz * barycentrics[0] +
        UnpackR10G10B10A2F(vertices[1].m_PackedNormal).xyz * barycentrics[1] +
        UnpackR10G10B10A2F(vertices[2].m_PackedNormal).xyz * barycentrics[2];
    
    float3 unpackedNormal = TwoChannelNormalX2(normalSample.xy);
    float3x3 TBN = CalculateTBNWithoutTangent(rayHitWorldPosition, geometricNormal, finalUV);
    result.m_Normal = normalize(mul(unpackedNormal, TBN));
    
    return result;
}
