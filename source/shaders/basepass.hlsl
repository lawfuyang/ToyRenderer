#include "common.hlsli"
#include "culling.hlsli"
#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "shared/MeshData.h"
#include "shared/BasePassStructs.h"
#include "shared/RawVertexFormat.h"
#include "shared/MaterialData.h"
#include "shared/IndirectArguments.h"

cbuffer g_PassConstantsBuffer : register(b0) { BasePassConstants g_BasePassConsts; }
StructuredBuffer<BasePassInstanceConstants> g_BasePassInstanceConsts : register(t0);
StructuredBuffer<RawVertexFormat> g_VirtualVertexBuffer : register(t1);
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t2);
StructuredBuffer<MaterialData> g_MaterialDataBuffer : register(t3);
StructuredBuffer<MeshletData> g_MeshletDataBuffer : register(t4);
StructuredBuffer<uint> g_MeshletVertexIDsBuffer : register(t5);
StructuredBuffer<uint> g_MeshletIndexIDsBuffer : register(t6);
StructuredBuffer<MeshletAmplificationData> g_MeshletAmplificationDataBuffer : register(t7);
Texture2D g_HZB : register(t8);
RWStructuredBuffer<uint> g_CullingCounters : register(u0);
Texture2D g_Textures[] : register(t0, space1);
sampler g_Samplers[SamplerIdx_Count] : register(s0); // Anisotropic Clamp, Wrap, Border, Mirror
SamplerState g_LinearClampMinReductionSampler : register(s4);

// forward shading specific resources
Texture2DArray g_DirLightShadowDepthTexture : register(t9);
Texture2D<uint> g_SSAOTexture : register(t10);
sampler g_PointClampSampler : register(s5);
SamplerComparisonState g_PointComparisonLessSampler : register(s6);
SamplerComparisonState g_LinearComparisonLessSampler : register(s7);

struct VertexOut
{
    float4 m_Position : SV_POSITION;
    float3 m_Normal : NORMAL;
    float3 m_WorldPosition : POSITION_WS;
    nointerpolation uint m_InstanceConstsIdx : TEXCOORD0;
    nointerpolation uint m_MeshletIdx : TEXCOORD1;
    nointerpolation uint m_MeshLOD : TEXCOORD2;
    float2 m_UV : TEXCOORD3;
};

groupshared MeshletPayload s_MeshletPayload;

[NumThreads(kNumThreadsPerWave, 1, 1)]
void AS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex
)
{
    const bool bDoFrustumCulling = g_BasePassConsts.m_CullingFlags & kCullingFlagFrustumCullingEnable;
    const bool bDoOcclusionCulling = g_BasePassConsts.m_CullingFlags & kCullingFlagOcclusionCullingEnable;
    const bool bDoConeCulling = g_BasePassConsts.m_CullingFlags & kCullingFlagMeshletConeCullingEnable;
    
    MeshletAmplificationData amplificationData = g_MeshletAmplificationDataBuffer[groupId.x];
    
    uint instanceConstIdx = amplificationData.m_InstanceConstIdx;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[instanceConstIdx];
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    MeshLODData meshLODData = meshData.m_MeshLODDatas[amplificationData.m_MeshLOD];
    
    bool bVisible = false;
    
    uint meshletIdx = amplificationData.m_MeshletGroupOffset + groupThreadID.x;
    if (meshletIdx < meshLODData.m_NumMeshlets)
    {
        MeshletData meshletData = g_MeshletDataBuffer[meshLODData.m_MeshletDataBufferIdx + meshletIdx];
        
        float3 sphereCenterWorldSpace = mul(float4(meshletData.m_BoundingSphere.xyz, 1.0f), instanceConsts.m_WorldMatrix).xyz;
        float3 sphereCenterViewSpace = mul(float4(sphereCenterWorldSpace, 1.0f), g_BasePassConsts.m_WorldToView).xyz;
        sphereCenterViewSpace.z *= -1.0f; // TODO: fix inverted view-space Z coord
        
        float sphereRadius = meshletData.m_BoundingSphere.w * GetMaxScaleFromWorldMatrix(instanceConsts.m_WorldMatrix);
        
        bVisible = !bDoFrustumCulling || FrustumCull(sphereCenterViewSpace, sphereRadius, g_BasePassConsts.m_Frustum);
        
    #if LATE_CULL
        if (bVisible && bDoOcclusionCulling)
        {
            bVisible = OcclusionCull(sphereCenterViewSpace,
                sphereRadius,
                g_BasePassConsts.m_NearPlane,
                g_BasePassConsts.m_P00,
                g_BasePassConsts.m_P11,
                g_HZB,
                g_BasePassConsts.m_HZBDimensions,
                g_LinearClampMinReductionSampler);
        }
    #endif // LATE_CULL
        
        if (bVisible && bDoConeCulling)
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
            coneAxisAndCutoff.xyz = mul(coneAxisAndCutoff.xyz, ToFloat3x3(g_BasePassConsts.m_WorldToView));
            coneAxisAndCutoff.z *= -1.0f; // TODO: fix inverted view-space Z coord
            
            bVisible = !ConeCull(sphereCenterViewSpace, sphereRadius, coneAxisAndCutoff.xyz, coneAxisAndCutoff.w);
        }
    }
    
    if (bVisible)
    {
    #if LATE_CULL
        InterlockedAdd(g_CullingCounters[kCullingLateMeshletsBufferCounterIdx], 1);
    #else
        InterlockedAdd(g_CullingCounters[kCullingEarlyMeshletsBufferCounterIdx], 1);
    #endif
        
        s_MeshletPayload.m_InstanceConstIdx = instanceConstIdx;
        s_MeshletPayload.m_MeshLOD = amplificationData.m_MeshLOD;
        
        uint payloadIdx = WavePrefixCountBits(bVisible);
        s_MeshletPayload.m_MeshletIndices[payloadIdx] = meshletIdx;
    }
    
    uint numVisible = WaveActiveCountBits(bVisible);
    DispatchMesh(numVisible, 1, 1, s_MeshletPayload);
}

[NumThreads(kMeshletShaderThreadGroupSize, 1, 1)]
[OutputTopology("triangle")]
void MS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex,
    in payload MeshletPayload inPayload,
    out vertices VertexOut meshletVertexOut[kMaxMeshletVertices],
    out indices uint3 meshletTrianglesOut[kMaxMeshletTriangles]
)
{
    uint meshletIdx = inPayload.m_MeshletIndices[groupId.x];
    uint outputIdx = groupThreadID.x;
    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inPayload.m_InstanceConstIdx];
    MeshData meshData = g_MeshDataBuffer[instanceConsts.m_MeshDataIdx];
    MeshLODData meshLODData = meshData.m_MeshLODDatas[inPayload.m_MeshLOD];
    MeshletData meshletData = g_MeshletDataBuffer[meshLODData.m_MeshletDataBufferIdx + meshletIdx];
    
    uint numVertices = (meshletData.m_VertexAndTriangleCount >> 0) & 0xFF;
    uint numPrimitives = (meshletData.m_VertexAndTriangleCount >> 8) & 0xFF;
    
    SetMeshOutputCounts(numVertices, numPrimitives);
    
    if (outputIdx < numVertices)
    {
        uint vertexIdx = g_MeshletVertexIDsBuffer[meshletData.m_MeshletVertexIDsBufferIdx + outputIdx];
        RawVertexFormat vertexInfo = g_VirtualVertexBuffer[vertexIdx];
    
        float4 vertexPosition = float4(vertexInfo.m_Position, 1.0f);
        float4 worldPos = mul(vertexPosition, instanceConsts.m_WorldMatrix);
    
        // Alien math to calculate the normal and tangent in world space, without inverse-transposing the world matrix
        // https://github.com/graphitemaster/normals_revisited
        // https://x.com/iquilezles/status/1866219178409316362
        // https://www.shadertoy.com/view/3s33zj
        float3x3 adjugateWorldMatrix = MakeAdjugateMatrix(instanceConsts.m_WorldMatrix);
        float3 UnpackedNormal = UnpackR10G10B10A2F(vertexInfo.m_PackedNormal).xyz;
        
        VertexOut vOut = (VertexOut)0;
        vOut.m_Position = mul(worldPos, g_BasePassConsts.m_WorldToClip);
        vOut.m_Normal = normalize(mul(UnpackedNormal, adjugateWorldMatrix));
        vOut.m_WorldPosition = worldPos.xyz;
        vOut.m_InstanceConstsIdx = inPayload.m_InstanceConstIdx;
        vOut.m_MeshletIdx = meshletIdx;
        vOut.m_MeshLOD = inPayload.m_MeshLOD;
        vOut.m_UV = vertexInfo.m_TexCoord;
        
        meshletVertexOut[outputIdx] = vOut;
    }
    
    if (outputIdx < numPrimitives)
    {
        uint packedIndices = g_MeshletIndexIDsBuffer[meshletData.m_MeshletIndexIDsBufferIdx + outputIdx];
    
        uint a = (packedIndices >> 0) & 0xFF;
        uint b = (packedIndices >> 8) & 0xFF;
        uint c = (packedIndices >> 16) & 0xFF;
        
        meshletTrianglesOut[outputIdx] = uint3(a, b, c);
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
        uint samplerIdx = materialData.m_AlbedoTextureSamplerAndDescriptorIndex >> 30;
        
        Texture2D albedoTexture = g_Textures[texIdx];
        float4 textureSample = albedoTexture.Sample(g_Samplers[samplerIdx], inUV);
        
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
        uint samplerIdx = materialData.m_NormalTextureSamplerAndDescriptorIndex >> 30;
        
        Texture2D normalTexture = g_Textures[texIdx];
        float3 sampledNormal = normalTexture.Sample(g_Samplers[samplerIdx], inUV).rgb;
        float3 unpackedNormal = TwoChannelNormalX2(sampledNormal.xy);
        
        float3x3 TBN = CalculateTBNWithoutTangent(inWorldPosition, inNormal, inUV);
        result.m_Normal = normalize(mul(unpackedNormal, TBN));
    }
    
    result.m_Roughness = 1.0f;
    result.m_Metallic = 0.0f;
    if (materialData.m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex >> 30;
                
        Texture2D mrtTexture = g_Textures[texIdx];
        float4 textureSample = mrtTexture.Sample(g_Samplers[samplerIdx], inUV);
        
        result.m_Roughness = textureSample.g;
        result.m_Metallic = textureSample.b;
    }
    
    result.m_Emissive = materialData.m_ConstEmissive;
    if (materialData.m_MaterialFlags & MaterialFlag_UseEmissiveTexture)
    {
        uint texIdx = NonUniformResourceIndex(materialData.m_EmissiveTextureSamplerAndDescriptorIndex & 0x3FFFFFFF);
        uint samplerIdx = materialData.m_EmissiveTextureSamplerAndDescriptorIndex >> 30;
        
        Texture2D emissiveTexture = g_Textures[texIdx];
        float4 textureSample = emissiveTexture.Sample(g_Samplers[samplerIdx], inUV);
        
        result.m_Emissive *= textureSample.rgb;
    }
    
    return result;
}

void PS_Main_GBuffer(
    in VertexOut inVertex,
    out uint4 outGBufferA : SV_Target0)
{
    GBufferParams gbufferParams = GetGBufferParams(inVertex.m_InstanceConstsIdx, inVertex.m_Normal, inVertex.m_UV, inVertex.m_WorldPosition);
    
    switch (g_BasePassConsts.m_DebugMode)
    {
        case kDeferredLightingDebugMode_ColorizeInstances:
            gbufferParams.m_DebugValue = QuickRandomFloat(inVertex.m_InstanceConstsIdx);
            break;
        case kDeferredLightingDebugMode_ColorizeMeshlets:
            gbufferParams.m_DebugValue = QuickRandomFloat(inVertex.m_MeshletIdx);
            break;
        case kDeferredLightingDebugMode_MeshLOD:
            gbufferParams.m_DebugValue = (float)inVertex.m_MeshLOD / 255.0f;
            break;
    }
    
    PackGBuffer(gbufferParams, outGBufferA);
}

void PS_Main_Forward(
    in VertexOut inVertex,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams = GetGBufferParams(inVertex.m_InstanceConstsIdx, inVertex.m_Normal, inVertex.m_UV, inVertex.m_WorldPosition);
    outColor = float4(gbufferParams.m_Albedo.rgb + gbufferParams.m_Emissive, gbufferParams.m_Albedo.a);
}
