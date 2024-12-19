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
    
    // Retrieve the instance constants for the given index
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inInstanceConstsIdx];
    
    // Retrieve the material data for the given instance
    MaterialData materialData = g_MaterialDataBuffer[instanceConsts.m_MaterialDataIdx];
        
    // Set the default albedo and alpha values
    result.m_Albedo = materialData.m_ConstDiffuse;
    result.m_Alpha = 1.0f;
    
    // Check if the material uses a diffuse texture
    if (materialData.m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        // Retrieve the texture index and sampler index
        uint texIdx = NonUniformResourceIndex(materialData.m_AlbedoTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_AlbedoTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV;
        finalUV *= materialData.m_AlbedoUVScale;
        finalUV += materialData.m_AlbedoUVOffset;
        
        // Retrieve the albedo texture and sample the texture at the given UV coordinates
        Texture2D albedoTexture = g_Textures[texIdx];
        float4 textureSample = albedoTexture.Sample(g_Samplers[samplerIdx], finalUV);
        
        // Update the albedo and alpha values with the sampled values
        result.m_Albedo = textureSample.rgb;
        result.m_Alpha = textureSample.a;
    }
    
#if ALPHA_MASK_MODE
    if (result.m_Alpha < materialData.m_AlphaCutoff)
    {
        discard;
    }
#endif
    
    // Set the default normal value
    result.m_Normal = inNormal;
    
    // Check if the material uses a normal texture
    if (materialData.m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        // Retrieve the texture index and sampler index
        uint texIdx = NonUniformResourceIndex(materialData.m_NormalTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_NormalTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV;
        finalUV *= materialData.m_NormalUVScale;
        finalUV += materialData.m_AlbedoUVOffset;
        
        Texture2D normalTexture = g_Textures[texIdx];
        float3 sampledNormal = normalTexture.Sample(g_Samplers[samplerIdx], finalUV).rgb;
        float3 unpackedNormal = TwoChannelNormalX2(sampledNormal.xy);
        
        float3x3 TBN = CalculateTBNWithoutTangent(inWorldPosition, inNormal, finalUV);
        result.m_Normal = normalize(mul(unpackedNormal, TBN));
    }
    
    // Set the default occlusion value
    result.m_Occlusion = 1.0f; // TODO: Implement occlusion calculation
    result.m_Roughness = 1.0f;
    result.m_Metallic = 0.0f;
    
    // Check if the material uses a metallic roughness texture
    if (materialData.m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        // Retrieve the texture index and sampler index
        uint texIdx = NonUniformResourceIndex(materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV;
        finalUV *= materialData.m_MetallicRoughnessUVScale;
        finalUV += materialData.m_MetallicRoughnessUVOffset;
        
        // Retrieve the metallic roughness texture and sample the texture at the given UV coordinates
        Texture2D mrtTexture = g_Textures[texIdx];
        float4 textureSample = mrtTexture.Sample(g_Samplers[samplerIdx], finalUV);
        
        // Update the roughness and metallic values with the sampled values
        result.m_Roughness = textureSample.g;
        result.m_Metallic = textureSample.b;
    }
    
    result.m_Emissive = materialData.m_Emissive;
    
    if (materialData.m_MaterialFlags & MaterialFlag_UseEmissiveTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_EmissiveTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_EmissiveTextureSamplerAndDescriptorIndex > 30;
        
        float2 finalUV = inUV;
        finalUV *= materialData.m_EmissiveUVScale;
        finalUV += materialData.m_EmissiveUVOffset;
        
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
    out float4 outGBufferA : SV_Target0,
    out float4 outGBufferB : SV_Target1,
    out float4 outGBufferC : SV_Target2,
    out uint4 outGBufferD : SV_Target3)
{
    GBufferParams gbufferParams = GetGBufferParams(inInstanceConstsIdx, inNormal, inUV, inWorldPosition);
    
    // for colorizing instances
    uint seed = inInstanceConstsIdx;
    float randFloat = QuickRandomFloat(seed);
    
    // Output to G-buffer targets
    outGBufferA = float4(gbufferParams.m_Albedo, randFloat);
    outGBufferB = float4(PackOctadehron(gbufferParams.m_Normal), 1, 1);
    outGBufferC = float4(gbufferParams.m_Occlusion, gbufferParams.m_Roughness, gbufferParams.m_Metallic, 1);
    outGBufferD = uint4(PackR9G9B9E5(gbufferParams.m_Emissive), 1, 1, 1);

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
    
    float3 lighting = DefaultLitBxDF(specular, gbufferParams.m_Roughness, gbufferParams.m_Albedo, gbufferParams.m_Normal, V, L);
    
    lighting += gbufferParams.m_Emissive;
    
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
    
    // NOTE: supposed to be viewspace normal, but i dont care for now because i plan to integrate AMD Brixelizer
    lighting += AmbientTerm(g_SSAOTexture, g_BasePassConsts.m_SSAOEnabled ? uint2(inPosition.xy) : uint2(0, 0), gbufferParams.m_Albedo, gbufferParams.m_Normal);
    
    outColor = float4(lighting, gbufferParams.m_Alpha);
}
