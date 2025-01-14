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
StructuredBuffer<MeshletData> g_MeshletDataBuffer : register(t4);
StructuredBuffer<uint> g_MeshletVertexIDsBuffer : register(t5);
StructuredBuffer<uint> g_MeshletIndexIDsBuffer : register(t6);
RWStructuredBuffer<uint> g_CullingCounters : register(u0);
Texture2D g_Textures[] : register(t0, space1);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror

// forward shading specific resources
Texture2DArray g_DirLightShadowDepthTexture : register(t4);
Texture2D<uint> g_SSAOTexture : register(t5);
sampler g_PointClampSampler : register(s4);
SamplerComparisonState g_PointComparisonLessSampler : register(s5);
SamplerComparisonState g_LinearComparisonLessSampler : register(s6);

struct VertexOut
{
    float4 m_Position : SV_POSITION;
    float3 m_Normal : NORMAL;
    float3 m_WorldPosition : POSITION_WS;
    nointerpolation uint m_InstanceConstsIdx : TEXCOORD0;
    nointerpolation uint m_MeshletIdx : TEXCOORD1;
    float2 m_UV : TEXCOORD2;
};

VertexOut GetVertexAttributes(BasePassInstanceConstants instanceConsts, MeshData meshData, uint instanceConstIdx, uint vertexIdx)
{
    VertexOut vOut = (VertexOut)0;
    
    RawVertexFormat vertexInfo = g_VirtualVertexBuffer[meshData.m_StartVertexLocation + vertexIdx];
    
    float4 position = float4(vertexInfo.m_Position, 1.0f);
    float4 worldPos = mul(position, instanceConsts.m_WorldMatrix);
    
    vOut.m_Position = mul(worldPos, g_BasePassConsts.m_ViewProjMatrix);
    
    // Alien math to calculate the normal and tangent in world space, without inverse-transposing the world matrix
    // https://github.com/graphitemaster/normals_revisited
    // https://x.com/iquilezles/status/1866219178409316362
    // https://www.shadertoy.com/view/3s33zj
    float3x3 adjugateWorldMatrix = MakeAdjugateMatrix(instanceConsts.m_WorldMatrix);
    
    float3 UnpackedNormal = UnpackR10G10B10A2F(vertexInfo.m_PackedNormal).xyz;
    vOut.m_Normal = normalize(mul(UnpackedNormal, adjugateWorldMatrix));
    
    vOut.m_UV = vertexInfo.m_TexCoord;
    vOut.m_WorldPosition = worldPos.xyz;
    vOut.m_InstanceConstsIdx = instanceConstIdx;

    return vOut;
}

void VS_Main(
    uint inInstanceConstIndex : INSTANCE_START_LOCATION, // per-instance attribute
    uint inVertexID : SV_VertexID,
    out VertexOut outVertex
)
{
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inInstanceConstIndex];
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    outVertex = GetVertexAttributes(instanceConsts, meshData, inInstanceConstIndex, inVertexID);
}

groupshared MeshletPayload s_MeshletPayload;

[NumThreads(32, 1, 1)]
void AS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex
)
{
    // temp until we move all of this shit to gpuculling.hlsl
    const uint kCullingMeshletsFrustumBufferCounterIdx = 2;
    const uint kCullingMeshletsConeBufferCounterIdx = 3;
    
    bool bVisible = false;
    
    uint meshletIdx = dispatchThreadID.x;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[g_BasePassConsts.m_InstanceConstIdx];
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    
    if (meshletIdx < meshData.m_MeshletCount)
    {
        MeshletData meshletData = g_MeshletDataBuffer[meshData.m_MeshletDataOffset + meshletIdx];
        
        float3 sphereCenterWorldSpace = mul(float4(meshletData.m_BoundingSphere.xyz, 1.0f), instanceConsts.m_WorldMatrix).xyz;
        float3 sphereCenterViewSpace = mul(float4(sphereCenterWorldSpace, 1.0f), g_BasePassConsts.m_ViewMatrix).xyz;
        sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
        
        float sphereRadius = max(max(instanceConsts.m_WorldMatrix._11, instanceConsts.m_WorldMatrix._22), instanceConsts.m_WorldMatrix._33) * meshletData.m_BoundingSphere.w;
        
        const bool bDoFrustumCulling = g_BasePassConsts.m_EnableFrustumCulling;
        bVisible = !bDoFrustumCulling || FrustumCull(sphereCenterViewSpace, sphereRadius, g_BasePassConsts.m_Frustum);
        
        if (bVisible)
        {
            InterlockedAdd(g_CullingCounters[kCullingMeshletsFrustumBufferCounterIdx], 1);
            
            if (g_BasePassConsts.m_bEnableMeshletConeCulling)
            {
                float4 coneAxisAndCutoff = float4(
                    (meshletData.m_ConeAxisAndCutoff >> 0) & 0xFF,
                    (meshletData.m_ConeAxisAndCutoff >> 8) & 0xFF,
                    (meshletData.m_ConeAxisAndCutoff >> 16) & 0xFF,
                    (meshletData.m_ConeAxisAndCutoff >> 24) & 0xFF
                );
                coneAxisAndCutoff /= 255.0f;
                
                // xyz = cone axis. need to map from [0,1] to [-1,1]
                coneAxisAndCutoff.xyz = coneAxisAndCutoff.xyz * 2.0f - 1.0f;
            
                coneAxisAndCutoff.xyz = normalize(mul(coneAxisAndCutoff.xyz, MakeAdjugateMatrix(instanceConsts.m_WorldMatrix)));
                coneAxisAndCutoff.xyz = mul(coneAxisAndCutoff.xyz, ToFloat3x3(g_BasePassConsts.m_ViewMatrix));
                coneAxisAndCutoff.z *= -1.0f;
                
                bVisible = !ConeCull(sphereCenterViewSpace, sphereRadius, coneAxisAndCutoff.xyz, coneAxisAndCutoff.w);
                if (bVisible)
                {
                    InterlockedAdd(g_CullingCounters[kCullingMeshletsConeBufferCounterIdx], 1);
                }
            }
        }
    }
    
    if (bVisible)
    {
        uint payloadIdx = WavePrefixCountBits(bVisible);
        s_MeshletPayload.m_MeshletIndices[payloadIdx] = meshletIdx;
    }
    
    uint numVisible = WaveActiveCountBits(bVisible);
    DispatchMesh(numVisible, 1, 1, s_MeshletPayload);
}

[NumThreads(kMaxMeshletSize, 1, 1)]
[OutputTopology("triangle")]
void MS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex,
    in payload MeshletPayload inPayload,
    out vertices VertexOut meshletVertexOut[kMaxMeshletSize],
    out indices uint3 meshletTrianglesOut[kMaxMeshletSize]
)
{
    uint meshletIdx = inPayload.m_MeshletIndices[groupId.x];
    uint meshletOutputIdx = groupThreadID.x;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[g_BasePassConsts.m_InstanceConstIdx];
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    MeshletData meshletData = g_MeshletDataBuffer[meshData.m_MeshletDataOffset + meshletIdx];
    
    uint numVertices = (meshletData.m_VertexAndTriangleCount >> 0) & 0xFF;
    uint numPrimitives = (meshletData.m_VertexAndTriangleCount >> 8) & 0xFF;
    
    SetMeshOutputCounts(numVertices, numPrimitives);
    
    if (meshletOutputIdx < numVertices)
    {
        uint vertexIdx = g_MeshletVertexIDsBuffer[meshletData.m_VertexBufferIdx + meshletOutputIdx];
        
        VertexOut vOut = GetVertexAttributes(instanceConsts, meshData, g_BasePassConsts.m_InstanceConstIdx, vertexIdx);
        vOut.m_MeshletIdx = meshletIdx;
        
        meshletVertexOut[meshletOutputIdx] = vOut;
    }
    
    if (meshletOutputIdx < numPrimitives)
    {
        uint packedIndices = g_MeshletIndexIDsBuffer[meshletData.m_IndicesBufferIdx + meshletOutputIdx];
    
        uint a = (packedIndices >> 0) & 0xFF;
        uint b = (packedIndices >> 8) & 0xFF;
        uint c = (packedIndices >> 16) & 0xFF;
        
        meshletTrianglesOut[meshletOutputIdx] = uint3(a, b, c);
    }
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
    in VertexOut inVertex,
    out uint4 outGBufferA : SV_Target0)
{
    GBufferParams gbufferParams = GetGBufferParams(inVertex.m_InstanceConstsIdx, inVertex.m_Normal, inVertex.m_UV, inVertex.m_WorldPosition);
    
    // for colorizing instances/meshlets
    uint seed = inVertex.m_MeshletIdx != 0 ? inVertex.m_MeshletIdx : inVertex.m_InstanceConstsIdx;
    gbufferParams.m_RandFloat = QuickRandomFloat(seed);
    
    PackGBuffer(gbufferParams, outGBufferA);
}

void PS_Main_Forward(
    in VertexOut inVertex,
    out float4 outColor : SV_Target)
{
    // Get the common base pass values for the current instance
    GBufferParams gbufferParams = GetGBufferParams(inVertex.m_InstanceConstsIdx, inVertex.m_Normal, inVertex.m_UV, inVertex.m_WorldPosition);
    
    const float materialSpecular = 0.5f; // TODO?
    float3 specular = ComputeF0(materialSpecular, gbufferParams.m_Metallic, gbufferParams.m_Metallic);
    float3 V = normalize(g_BasePassConsts.m_CameraOrigin - inVertex.m_WorldPosition);
    float3 L = g_BasePassConsts.m_DirectionalLightVector;
    
    float3 lighting = DefaultLitBxDF(specular, gbufferParams.m_Roughness, gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal, V, L);
    
    ShadowFilteringParams shadowFilteringParams;
    shadowFilteringParams.m_WorldPosition = inVertex.m_WorldPosition;
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
    lighting += AmbientTerm(g_SSAOTexture, g_BasePassConsts.m_SSAOEnabled ? uint2(inVertex.m_WorldPosition.xy) : uint2(0, 0), gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal);
    
    outColor = float4(lighting, gbufferParams.m_Albedo.a);
}
