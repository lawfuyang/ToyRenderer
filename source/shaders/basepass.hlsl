#include "common.hlsli"
#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "shared/MeshData.h"
#include "shared/BasePassStructs.h"
#include "shared/CommonConsts.h"
#include "shared/RawVertexFormat.h"
#include "shared/MaterialData.h"

cbuffer g_PassConstantsBuffer : register(b0) { BasePassConstants g_BasePassConsts; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<RawVertexFormat> g_VirtualVertexBuffer : register(t1);
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t2);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t3);
Texture2D g_Textures[] : register(t0, space1);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror

// forward shading specific resources
Texture2DArray g_DirLightShadowDepthTexture : register(t4);
Texture2D<uint> g_SSAOTexture : register(t5);
sampler g_PointClampSampler : register(s4);
SamplerComparisonState g_PointComparisonLessSampler : register(s5);
SamplerComparisonState g_LinearComparisonLessSampler : register(s6);

void VS_Main(
    uint inInstanceConstIndex : INSTANCE_START_LOCATION, // per-instance attribute
    uint inVertexID : SV_VertexID,
    out float4 outPosition : SV_POSITION,
    out float3 outNormal : NORMAL,
    out float3 outWorldPosition : POSITION_WS,
    out nointerpolation uint outInstanceConstsIdx : TEXCOORD0,
    out float2 outUV : TEXCOORD1
)
{
    // Retrieve the instance constants for the given index
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inInstanceConstIndex];
    
    // Retrieve the mesh data for the current instance
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    
    // Retrieve the vertex information for the current vertex
    RawVertexFormat vertexInfo = g_VirtualVertexBuffer[meshData.m_StartVertexLocation + inVertexID];
    
    // Transform the vertex position to world space
    float4 position = float4(vertexInfo.m_Position, 1.0f);
    float4 worldPos = mul(position, instanceConsts.m_WorldMatrix);
    
    // Transform the world space position to clip space
    outPosition = mul(worldPos, g_BasePassConsts.m_ViewProjMatrix);
    
    // Pass the instance constants index to the pixel shader
    outInstanceConstsIdx = inInstanceConstIndex;
    
    // Alien math to calculate the normal and tangent in world space, without inverse-transposing the world matrix
    // https://github.com/graphitemaster/normals_revisited
    // https://x.com/iquilezles/status/1866219178409316362
    // https://www.shadertoy.com/view/3s33zj
    float3x3 adjugateWorldMatrix = MakeAdjugateMatrix(instanceConsts.m_WorldMatrix);
    
    // Transform the vertex normal to world space and normalize it
    float3 UnpackedNormal = UnpackR10G10B10A2F(vertexInfo.m_PackedNormal).xyz;
    outNormal = normalize(mul(UnpackedNormal, adjugateWorldMatrix));
    
    // Pass the vertex texture coordinates to the pixel shader
    outUV = vertexInfo.m_TexCoord;
    
    // Pass the world space position to the pixel shader
    outWorldPosition = worldPos.xyz;
}

// Christian Schuler, "Normal Mapping without Precomputed Tangents", ShaderX 5, Chapter 2.6, pp. 131-140
// See also follow-up blog post: http://www.thetenthplanet.de/archives/1180
float3x3 CalculateTBNWithoutTangent(float3 p, float3 n, float2 tex)
{
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(tex);
    float2 duv2 = ddy(tex);

    float3x3 M = float3x3(dp1, dp2, cross(dp1, dp2));
    float2x3 inverseM = float2x3(cross(M[1], M[2]), cross(M[2], M[0]));
    float3 t = normalize(mul(float2(duv1.x, duv2.x), inverseM));
    float3 b = normalize(mul(float2(duv1.y, duv2.y), inverseM));
    return float3x3(t, b, n);
}

// Unpacks a 2 channel normal to xyz
float3 TwoChannelNormalX2(float2 normal)
{
    float2 xy = 2.0f * normal - 1.0f;
    float z = sqrt(1 - dot(xy, xy));
    return float3(xy.x, xy.y, z);
}

GBufferParams GetGBufferParams(
    uint inInstanceConstsIdx,
    float3 inNormal,
    float2 inUV,
    float3 inWorldPosition)
{
    GBufferParams result;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inInstanceConstsIdx];
    MaterialData materialData = g_MaterialDataBuffer[instanceConsts.m_MaterialDataIdx];
        
    result.m_Albedo = materialData.m_ConstAlbedo;
    if (materialData.m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_AlbedoTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_AlbedoTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV * materialData.m_AlbedoUVOffsetAndScale.zw + materialData.m_AlbedoUVOffsetAndScale.xy;
        
        Texture2D albedoTexture = g_Textures[texIdx];
        float4 textureSample = albedoTexture.Sample(g_Samplers[samplerIdx], finalUV);
        
        result.m_Albedo.rgb *= textureSample.rgb;
        result.m_Albedo.a *= textureSample.a;
    }
    
#if ALPHA_MASK_MODE
    if (result.m_Albedo.a < materialData.m_AlphaCutoff)
    {
        discard;
    }
#endif
    
    result.m_Normal = inNormal;
    if (materialData.m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_NormalTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_NormalTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV * materialData.m_NormalUVOffsetAndScale.zw + materialData.m_NormalUVOffsetAndScale.xy;
        
        Texture2D normalTexture = g_Textures[texIdx];
        float3 sampledNormal = normalTexture.Sample(g_Samplers[samplerIdx], finalUV).rgb;
        float3 unpackedNormal = TwoChannelNormalX2(sampledNormal.xy);
        
        float3x3 TBN = CalculateTBNWithoutTangent(inWorldPosition, inNormal, finalUV);
        result.m_Normal = normalize(mul(unpackedNormal, TBN));
    }
    
    result.m_Roughness = 1.0f;
    result.m_Metallic = 0.0f;
    if (materialData.m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV * materialData.m_MetallicRoughnessUVOffsetAndScale.zw + materialData.m_MetallicRoughnessUVOffsetAndScale.xy;
        
        Texture2D mrtTexture = g_Textures[texIdx];
        float4 textureSample = mrtTexture.Sample(g_Samplers[samplerIdx], finalUV);
        
        result.m_Roughness = textureSample.g;
        result.m_Metallic = textureSample.b;
    }
    
    result.m_Emissive = materialData.m_ConstEmissive;
    if (materialData.m_MaterialFlags & MaterialFlag_UseEmissiveTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_EmissiveTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_EmissiveTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV * materialData.m_EmissiveUVOffsetAndScale.zw + materialData.m_EmissiveUVOffsetAndScale.xy;
        
        Texture2D emissiveTexture = g_Textures[texIdx];
        float4 textureSample = emissiveTexture.Sample(g_Samplers[samplerIdx], finalUV);
        
        result.m_Emissive *= textureSample.rgb;
    }
    
    return result;
}

void PS_Main_GBuffer(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : NORMAL,
    in float3 inWorldPosition : POSITION_WS,
    in uint inInstanceConstsIdx : TEXCOORD0,
    in float2 inUV : TEXCOORD1,
    out uint4 outGBufferA : SV_Target0)
{
    GBufferParams gbufferParams = GetGBufferParams(inInstanceConstsIdx, inNormal, inUV, inWorldPosition);
    
    // for colorizing instances
    uint seed = inInstanceConstsIdx;
    gbufferParams.m_RandFloat = QuickRandomFloat(seed);
    
    PackGBuffer(gbufferParams, outGBufferA);
}

void PS_Main_Forward(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : NORMAL,
    in float3 inWorldPosition : POSITION_WS,
    in uint inInstanceConstsIdx : TEXCOORD0,
    in float2 inUV : TEXCOORD1,
    out float4 outColor : SV_Target)
{
    // Get the common base pass values for the current instance
    GBufferParams gbufferParams = GetGBufferParams(inInstanceConstsIdx, inNormal, inUV, inWorldPosition);
    
    const float materialSpecular = 0.5f; // TODO?
    float3 specular = ComputeF0(materialSpecular, gbufferParams.m_Metallic, gbufferParams.m_Metallic);
    float3 V = normalize(g_BasePassConsts.m_CameraOrigin - inWorldPosition);
    float3 L = g_BasePassConsts.m_DirectionalLightVector;
    
    float3 lighting = DefaultLitBxDF(specular, gbufferParams.m_Roughness, gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal, V, L);
    
    ShadowFilteringParams shadowFilteringParams;
    shadowFilteringParams.m_WorldPosition = inWorldPosition;
    shadowFilteringParams.m_CameraPosition = g_BasePassConsts.m_CameraOrigin;
    shadowFilteringParams.m_CSMDistances = g_BasePassConsts.m_CSMDistances;
    shadowFilteringParams.m_DirLightViewProj = g_BasePassConsts.m_DirLightViewProj;
    shadowFilteringParams.m_InvShadowMapResolution = g_BasePassConsts.m_InvShadowMapResolution;
    shadowFilteringParams.m_DirLightShadowDepthTexture = g_DirLightShadowDepthTexture;
    shadowFilteringParams.m_PointClampSampler = g_PointClampSampler;
    shadowFilteringParams.m_PointComparisonLessSampler = g_PointComparisonLessSampler;
    shadowFilteringParams.m_LinearComparisonLessSampler = g_LinearComparisonLessSampler;
    
    // Compute the shadow factor using shadow filtering
    float shadowFactor = ShadowFiltering(shadowFilteringParams);
    lighting *= shadowFactor;
    
    lighting += gbufferParams.m_Emissive;
    
    // NOTE: supposed to be viewspace normal, but i dont care for now because i plan to integrate AMD Brixelizer
    lighting += AmbientTerm(g_SSAOTexture, g_BasePassConsts.m_SSAOEnabled ? uint2(inPosition.xy) : uint2(0, 0), gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal);
    
    outColor = float4(lighting, gbufferParams.m_Albedo.a);
}
