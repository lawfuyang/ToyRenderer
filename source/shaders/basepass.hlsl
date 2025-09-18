#include "toyrenderer_common.hlsli"
#include "culling.hlsli"
#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

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
SamplerState g_AnisotropicClampSampler : register(s0);
SamplerState g_AnisotropicWrapSampler : register(s1);
SamplerState g_PointClampMaxReductionSampler : register(s2);
SamplerState g_PointWrapMaxReductionSampler : register(s3);
SamplerState g_LinearClampMinReductionSampler : register(s4);

struct VertexOut
{
    float4 m_Position : SV_POSITION;
    nointerpolation uint m_InstanceConstsIdx : TEXCOORD0;
    nointerpolation uint m_MeshletIdx : TEXCOORD1;
    nointerpolation uint m_MeshLOD : TEXCOORD2;
    float3 m_Normal : TEXCOORD3;
    float2 m_UV : TEXCOORD4;
    float3 m_WorldPosition : TEXCOORD5;
    float3 m_PrevWorldPosition : TEXCOORD6;
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
        
        if (bVisible && bDoOcclusionCulling)
        {
            OcclusionCullArguments occlusionCullArguments;
            occlusionCullArguments.m_SphereCenterViewSpace = sphereCenterViewSpace;
            occlusionCullArguments.m_Radius = sphereRadius;
            occlusionCullArguments.m_NearPlane = g_BasePassConsts.m_NearPlane;
            occlusionCullArguments.m_P00 = g_BasePassConsts.m_P00;
            occlusionCullArguments.m_P11 = g_BasePassConsts.m_P11;
            occlusionCullArguments.m_HZB = g_HZB;
            occlusionCullArguments.m_HZBDimensions = g_BasePassConsts.m_HZBDimensions;
            occlusionCullArguments.m_LinearClampMinReductionSampler = g_LinearClampMinReductionSampler;
        
            bVisible = OcclusionCull(occlusionCullArguments);
        }
        
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
        float4 prevWorldPos = mul(vertexPosition, instanceConsts.m_PrevWorldMatrix);
    
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
        vOut.m_PrevWorldPosition = prevWorldPos.xyz;
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

GBufferParams GetGBufferParams(VertexOut inVertex)
{    
    BasePassInstanceConstants instanceConsts = g_BasePassInstanceConsts[inVertex.m_InstanceConstsIdx];
    MaterialData materialData = g_MaterialDataBuffer[instanceConsts.m_MaterialDataIdx];
    
    GetCommonGBufferParamsArguments getCommonGBufferParamsArguments = CreateDefaultGetCommonGBufferParamsArguments();

    getCommonGBufferParamsArguments.m_TexCoord = inVertex.m_UV;
    getCommonGBufferParamsArguments.m_WorldPosition = inVertex.m_WorldPosition;
    getCommonGBufferParamsArguments.m_Normal = inVertex.m_Normal;
    getCommonGBufferParamsArguments.m_MaterialData = materialData;
    getCommonGBufferParamsArguments.m_AnisotropicClampSampler = g_AnisotropicClampSampler;
    getCommonGBufferParamsArguments.m_AnisotropicWrapSampler = g_AnisotropicWrapSampler;
    getCommonGBufferParamsArguments.m_PointClampMaxReductionSampler = g_PointClampMaxReductionSampler;
    getCommonGBufferParamsArguments.m_PointWrapMaxReductionSampler = g_PointWrapMaxReductionSampler;
    getCommonGBufferParamsArguments.m_bEnableSamplerFeedback = g_BasePassConsts.m_bWriteSamplerFeedback;
    getCommonGBufferParamsArguments.m_bVisualizeMinMipTilesOnAlbedoOutput = g_BasePassConsts.m_bVisualizeMinMipTilesOnAlbedoOutput;
    
    GBufferParams result = GetCommonGBufferParams(getCommonGBufferParamsArguments);
    
#if ALPHA_MASK_MODE
    if (result.m_Albedo.a < materialData.m_AlphaCutoff)
    {
        discard;
    }
#endif // ALPHA_MASK_MODE
    
    float4 prevClipPosition = mul(float4(inVertex.m_PrevWorldPosition, 1), g_BasePassConsts.m_PrevWorldToClip);
    prevClipPosition.xy /= prevClipPosition.w;
    float4 currentClipPosition = mul(float4(inVertex.m_WorldPosition, 1), g_BasePassConsts.m_WorldToClip);
    currentClipPosition.xy /= currentClipPosition.w;
    
    result.m_Motion = ClipXYToUV(currentClipPosition.xy - prevClipPosition.xy);
    
    return result;
}

void PS_Main_GBuffer(
    in VertexOut inVertex,
    out uint4 outGBufferA : SV_Target0,
    out float4 outGBufferMotion : SV_Target1)
{
    GBufferParams gbufferParams = GetGBufferParams(inVertex);
    
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
    outGBufferMotion = float4(gbufferParams.m_Motion, 0.0f, 0.0f);
}

void PS_Main_Forward(
    in VertexOut inVertex,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams = GetGBufferParams(inVertex);
    outColor = float4(gbufferParams.m_Albedo.rgb + gbufferParams.m_Emissive, gbufferParams.m_Albedo.a);
}
