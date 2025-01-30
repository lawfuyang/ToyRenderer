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
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t5);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t6);
StructuredBuffer<uint> g_GlobalIndexIDsBuffer : register(t7);
Texture2D g_Textures[] : register(t0, space1);
RWTexture2D<float4> g_ShadowMaskOutput : register(u0);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror

struct RayPayload
{
    float m_HitT; // not used
};

struct IntersectionAttributes
{
    float2 uv;
};

[shader("raygeneration")]
void RT_RayGen()
{
    uint2 rayIdx = DispatchRaysIndex().xy;
    float depth = g_DepthBuffer[rayIdx.xy].x;
    
    if (depth == kFarDepth)
    {
        return;
    }
    
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[rayIdx.xy], gbufferParams);
    
    float2 screenUV = (rayIdx + float2(0.5f, 0.5f)) / g_ShadowMaskConsts.m_OutputResolution;
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_ShadowMaskConsts.m_ClipToWorld);
    
    // empirical offset to remove shadow acne
    float3 rayOriginOffset = gbufferParams.m_Normal * 0.01f;
    
    RayDesc rayDesc;
    rayDesc.Origin = worldPosition + rayOriginOffset;
    rayDesc.Direction = g_ShadowMaskConsts.m_DirectionalLightDirection;
    rayDesc.TMin = 0.1f;
    rayDesc.TMax = kKindaBigNumber;
    
    RayPayload payload;
    
    TraceRay(
        g_SceneTLAS,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
        0xFF,
        0,
        0,
        0,
        rayDesc,
        payload);
}

[shader("miss")]
void RT_Miss(inout RayPayload payload : SV_RayPayload)
{
    g_ShadowMaskOutput[DispatchRaysIndex().xy] = 1.0f;
}

[shader("closesthit")]
void RT_ClosestHit(inout RayPayload payload : SV_RayPayload, in IntersectionAttributes attributes : SV_IntersectionAttributes)
{
    g_ShadowMaskOutput[DispatchRaysIndex().xy] = 0.0f;
}

[shader("anyhit")]
void RT_AnyHit(inout RayPayload payload : SV_RayPayload, in IntersectionAttributes attributes : SV_IntersectionAttributes)
{
    uint instanceID = InstanceID();
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceID];
    MaterialData materialData = g_MaterialDataBuffer[instanceConsts.m_MaterialDataIdx];
    
    if (materialData.m_AlphaCutoff == 0.0f)
    {
        // it's an opaque material with no alpha mask - accept the hit and end the search
        AcceptHitAndEndSearch();
    }
    
    if (materialData.m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        uint triangleID = PrimitiveIndex();
        MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
        
        uint indices[3] =
        {
            g_GlobalIndexIDsBuffer[triangleID * 3 + 0],
            g_GlobalIndexIDsBuffer[triangleID * 3 + 1],
            g_GlobalIndexIDsBuffer[triangleID * 3 + 2],
        };
        
        float2 vertexUVs[3] =
        {
            g_GlobalVertexBuffer[indices[0]].m_TexCoord,
            g_GlobalVertexBuffer[indices[1]].m_TexCoord,
            g_GlobalVertexBuffer[indices[2]].m_TexCoord,
        };
        
        float barycentrics[3] = { (1.0f - attributes.uv.x - attributes.uv.y), attributes.uv.x, attributes.uv.y };
        float2 finalUV = vertexUVs[0] * barycentrics[0] + vertexUVs[1] * barycentrics[1] + vertexUVs[2] * barycentrics[2];
        
        uint texIdx = NonUniformResourceIndex(materialData.m_AlbedoTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_AlbedoTextureSamplerAndDescriptorIndex >> 30;
        
        Texture2D albedoTexture = g_Textures[texIdx];
        
        // TODO: use 'SampleGrad'
        float4 textureSample = albedoTexture.SampleLevel(g_Samplers[samplerIdx], finalUV, 0);
        
        float alpha = materialData.m_ConstAlbedo.a * textureSample.a;
        
        if (alpha > materialData.m_AlphaCutoff)
        {
            // alpha test passed - accept the hit and end the search
            AcceptHitAndEndSearch();
        }
    }
}
