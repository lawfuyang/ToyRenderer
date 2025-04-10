#pragma once

#include "lightingcommon.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

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
GBufferParams GetRayHitInstanceGBufferParams(RayQuery<kRayQueryFlags> rayQuery, Texture2D bindlessTextures[], out float3 rayHitWorldPosition, GetRayHitInstanceGBufferParamsArguments inArgs)
{
    StructuredBuffer<BasePassInstanceConstants> basePassInstanceConstantsBuffer = inArgs.m_BasePassInstanceConstantsBuffer;
    StructuredBuffer<MaterialData> materialDataBuffer = inArgs.m_MaterialDataBuffer;
    StructuredBuffer<MeshData> meshDataBuffer = inArgs.m_MeshDataBuffer;
    StructuredBuffer<uint> globalIndexIDsBuffer = inArgs.m_GlobalIndexIDsBuffer;
    StructuredBuffer<RawVertexFormat> globalVertexBuffer = inArgs.m_GlobalVertexBuffer;
    sampler samplers[SamplerIdx_Count] = inArgs.m_Samplers;
    
    uint instanceID = rayQuery.CommittedInstanceID();
    uint primitiveIndex = rayQuery.CommittedPrimitiveIndex();
    float2 attribBarycentrics = rayQuery.CommittedTriangleBarycentrics();
    
    BasePassInstanceConstants instanceConsts = basePassInstanceConstantsBuffer[instanceID];
    MaterialData materialData = materialDataBuffer[instanceConsts.m_MaterialDataIdx];
    
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
    
    rayHitWorldPosition = float3(0.0f, 0.0f, 0.0f);
    float3 rayHitNormal = float3(0.0f, 0.0f, 0.0f);
    float2 rayHitUV = float2(0.0f, 0.0f);
    
    for (uint i = 0; i < 3; i++)
    {
        rayHitWorldPosition += vertices[i].m_Position * barycentrics[i];
        rayHitNormal += UnpackR10G10B10A2F(vertices[i].m_PackedNormal).xyz * barycentrics[i];
        rayHitUV += vertices[i].m_TexCoord * barycentrics[i];
    }
    
    rayHitWorldPosition = mul(rayQuery.CommittedObjectToWorld3x4(), float4(rayHitWorldPosition, 1.0f)).xyz;
    
    GetCommonGBufferParamsArguments getCommonGBufferParamsArguments;
    getCommonGBufferParamsArguments.m_TexCoord = rayHitUV;
    getCommonGBufferParamsArguments.m_WorldPosition = rayHitWorldPosition;
    getCommonGBufferParamsArguments.m_Normal = rayHitNormal;
    getCommonGBufferParamsArguments.m_MaterialData = materialData;
    getCommonGBufferParamsArguments.m_Samplers = samplers;
    
    return GetCommonGBufferParams(getCommonGBufferParamsArguments, bindlessTextures);
}
