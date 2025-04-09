#pragma once

#include "lightingcommon.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

struct SampleMaterialValueArgs
{
    float2 m_TexCoord;
    uint m_MaterialFlag; // preferably compile-time const
    MaterialData m_MaterialData;
    sampler m_Samplers[SamplerIdx_Count];
    float4 m_DefaultValue; // preferably compile-time const
};

float4 SampleMaterialValue(SampleMaterialValueArgs args, Texture2D bindlessTextures[])
{
    float2 texCoord = args.m_TexCoord;
    uint materialFlag = args.m_MaterialFlag;
    MaterialData materialData = args.m_MaterialData;
    sampler samplers[SamplerIdx_Count] = args.m_Samplers;
    float4 defaultValue = args.m_DefaultValue;
    
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

struct GetRayHitInstanceGBufferParamsArguments
{
    StructuredBuffer<BasePassInstanceConstants> m_BasePassInstanceConstantsBuffer;
    StructuredBuffer<MaterialData> m_MaterialDataBuffer;
    StructuredBuffer<MeshData> m_MeshDataBuffer;
    StructuredBuffer<uint> m_GlobalIndexIDsBuffer;
    StructuredBuffer<RawVertexFormat> m_GlobalVertexBuffer;
    sampler m_Samplers[SamplerIdx_Count];
};

template<uint kRayQueryFlags>
GBufferParams GetRayHitInstanceGBufferParams(
    RayQuery<kRayQueryFlags> rayQuery,
    Texture2D bindlessTextures[],
    out float3 rayHitWorldPosition,
    GetRayHitInstanceGBufferParamsArguments inArgs)
{
    StructuredBuffer<BasePassInstanceConstants> basePassInstanceConstantsBuffer = inArgs.m_BasePassInstanceConstantsBuffer;
    StructuredBuffer<MaterialData> materialDataBuffer = inArgs.m_MaterialDataBuffer;
    StructuredBuffer<MeshData> meshDataBuffer = inArgs.m_MeshDataBuffer;
    StructuredBuffer<uint> globalIndexIDsBuffer = inArgs.m_GlobalIndexIDsBuffer;
    StructuredBuffer<RawVertexFormat> globalVertexBuffer = inArgs.m_GlobalVertexBuffer;
    sampler samplers[SamplerIdx_Count] = inArgs.m_Samplers;
    
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
    
    SampleMaterialValueArgs sampleArgs;
    sampleArgs.m_TexCoord = finalUV;
    sampleArgs.m_MaterialData = materialData;
    sampleArgs.m_Samplers = samplers;
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseDiffuseTexture;
    sampleArgs.m_DefaultValue = float4(1, 1, 1, 1);
    float4 albedoSample = SampleMaterialValue(sampleArgs, bindlessTextures);
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseNormalTexture;
    sampleArgs.m_DefaultValue = float4(0.5f, 0.5f, 1.0f, 0.0f);
    float4 normalSample = SampleMaterialValue(sampleArgs, bindlessTextures);
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseMetallicRoughnessTexture;
    sampleArgs.m_DefaultValue = float4(0.0f, 1.0f, 0.0f, 0.0f);
    float4 metalRoughnessSample = SampleMaterialValue(sampleArgs, bindlessTextures);
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseEmissiveTexture;
    sampleArgs.m_DefaultValue = float4(1, 1, 1, 0);
    float4 emissiveSample = SampleMaterialValue(sampleArgs, bindlessTextures);
   
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
