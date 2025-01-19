#include "common.hlsli"

#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "shared/DeferredLightingStructs.h"
#include "shared/IndirectArguments.h"

// NOTE: this is being used in multiple Shaders in this file
SamplerState g_PointClampSampler : register(s0);

cbuffer g_DeferredLightingPassConstantsBuffer : register(b0) { DeferredLightingConsts g_DeferredLightingConsts; }
Texture2D<uint4> g_GBufferA : register(t0);
Texture2D g_DepthBuffer : register(t1);
Texture2D<uint> g_SSAOTexture : register(t2);
Texture2D g_ShadowMaskTexture : register(t3);
StructuredBuffer<uint2> g_TileOffsets : register(t99);
RWTexture2D<float3> g_LightingOutput : register(u0);

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[inPosition.xy], gbufferParams);
    
    // Convert screen UV coordinates and depth to world position
    float depth = g_DepthBuffer[inPosition.xy].x;
    float3 worldPosition = ScreenUVToWorldPosition(inUV, depth, g_DeferredLightingConsts.m_InvViewProjMatrix);
    
    const float materialSpecular = 0.5f; // TODO?
    float3 diffuse = ComputeDiffuseColor(gbufferParams.m_Albedo.rgb, gbufferParams.m_Metallic);
    float3 specular = ComputeF0(materialSpecular, gbufferParams.m_Albedo.rgb, gbufferParams.m_Metallic);
    
    float3 V = normalize(g_DeferredLightingConsts.m_CameraOrigin - worldPosition);
    float3 L = g_DeferredLightingConsts.m_DirectionalLightVector;
    
    float3 lighting = DefaultLitBxDF(specular, gbufferParams.m_Roughness, diffuse, gbufferParams.m_Normal, V, L);
    
    // Retrieve the shadow factor from the shadow mask texture
    float shadowFactor = g_ShadowMaskTexture.SampleLevel(g_PointClampSampler, inUV, 0).r;
    lighting *= shadowFactor;
    
    lighting += gbufferParams.m_Emissive;
    
    // NOTE: supposed to be viewspace normal, but i dont care
    float3 ambientTerm = AmbientTerm(g_SSAOTexture, g_DeferredLightingConsts.m_SSAOEnabled ? inPosition.xy : uint2(0, 0), gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal);
    lighting += ambientTerm;
    
    outColor = float4(lighting, 1.0f);
}

void PS_Main_Debug(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[inPosition.xy], gbufferParams);
    
    float shadowFactor = g_ShadowMaskTexture.SampleLevel(g_PointClampSampler, inUV, 0).r;
    
    float3 rgb = 0.0f;
    
    if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_LightingOnly)
    {
        float lightingOnlyShadowFactor = max(0.05f, shadowFactor); // Prevent the shadow factor from being too low to avoid outputting pure black pixels
        rgb = dot(gbufferParams.m_Normal, g_DeferredLightingConsts.m_DirectionalLightVector).xxx * lightingOnlyShadowFactor;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_ColorizeInstances || g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_ColorizeMeshlets)
    {
        uint seed = gbufferParams.m_DebugValue * 255.0f;
        
        float randR = QuickRandomFloat(seed);
        float randG = QuickRandomFloat(seed);
        float randB = QuickRandomFloat(seed);
        rgb = float3(randR, randG, randB);
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Albedo)
    {
        rgb = gbufferParams.m_Albedo.rgb;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Normal)
    {
        rgb = gbufferParams.m_Normal;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Emissive)
    {
        rgb = gbufferParams.m_Emissive;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Metalness)
    {
        rgb = gbufferParams.m_Metallic.rrr;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Roughness)
    {
        rgb = gbufferParams.m_Roughness.rrr;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_AmbientOcclusion)
    {
        rgb = g_SSAOTexture[inPosition.xy].rrr / 255.0f;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_Ambient)
    {
        rgb = AmbientTerm(g_SSAOTexture, g_DeferredLightingConsts.m_SSAOEnabled ? inPosition.xy : uint2(0, 0), gbufferParams.m_Albedo.rgb, gbufferParams.m_Normal);
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_ShadowMask)
    {
        rgb = shadowFactor.rrr;
    }
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_MeshLOD)
    {
        static const float3 kLODColors[8] =
            {
                { 1.0f, 0.0f, 0.0f },   // LOD 0 - Red (highest detail, most important)
                { 1.0f, 0.5f, 0.0f },   // LOD 1 - Orange
                { 1.0f, 1.0f, 0.0f },   // LOD 2 - Yellow
                { 0.5f, 1.0f, 0.0f },   // LOD 3 - Light Green
                { 0.0f, 1.0f, 0.0f },   // LOD 4 - Green
                { 0.0f, 0.5f, 1.0f },   // LOD 5 - Light Blue
                { 0.0f, 0.0f, 1.0f },   // LOD 6 - Blue
                { 0.5f, 0.0f, 1.0f }    // LOD 7 - Purple (lowest detail, least important)
            };
        
        rgb = kLODColors[gbufferParams.m_DebugValue * 255];
    }
    
    outColor = float4(rgb, 1.0f);
}
