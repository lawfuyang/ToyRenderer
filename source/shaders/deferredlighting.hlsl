#include "common.hlsli"

#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "shared/CommonConsts.h"
#include "shared/DeferredLightingStructs.h"
#include "shared/IndirectArguments.h"

// NOTE: this is being used in multiple Shaders in this file
SamplerState g_PointClampSampler : register(s0);

cbuffer g_DeferredLightingPassConstantsBuffer : register(b0) { DeferredLightingConsts g_DeferredLightingConsts; }
Texture2D g_GBufferAlbedo : register(t0);
Texture2D g_GBufferNormal : register(t1);
Texture2D g_GBufferPBR : register(t2);
Texture2D<uint> g_GBufferEmissive : register(t3);
Texture2D g_DepthBuffer : register(t4);
Texture2D<uint> g_SSAOTexture : register(t5);
Texture2D g_ShadowMaskTexture : register(t6);
StructuredBuffer<uint2> g_TileOffsets : register(t99);
RWTexture2D<float3> g_LightingOutput : register(u0);

float3 EvaluteLighting(uint2 screenTexel, float2 screenUV)
{
    // Retrieve the depth value from the depth buffer
    float depth = g_DepthBuffer[screenTexel].x;
    
    // If the depth value is too far, set the output to black
    if (depth == kFarDepth)
    {
        return float3(0, 0, 0);
    }

    float3 albedo = g_GBufferAlbedo[screenTexel].rgb;
    float3 normal = UnpackOctadehron(g_GBufferNormal[screenTexel].rg); // NOTE: supposed to be viewspace normal, but i dont care for now because i plan to integrate AMD Brixelizer
    float3 ambientTerm = AmbientTerm(g_SSAOTexture, g_DeferredLightingConsts.m_SSAOEnabled ? screenTexel : uint2(0, 0), albedo, normal);
    
    // Convert screen UV coordinates and depth to world position
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_DeferredLightingConsts.m_InvViewProjMatrix);
    
    // Retrieve the PBR values from the G-buffer
    float3 pbr = g_GBufferPBR[screenTexel].rgb;
    float occlusion = pbr.x;
    float roughness = pbr.y;
    float metallic = pbr.z;
    
    float3 emissive = UnpackR9G9B9E5(g_GBufferEmissive[screenTexel].r);
    
    const float materialSpecular = 0.5f; // TODO?
    float3 diffuse = ComputeDiffuseColor(albedo, metallic);
    float3 specular = ComputeF0(materialSpecular, albedo, metallic);
    
    float3 V = normalize(g_DeferredLightingConsts.m_CameraOrigin - worldPosition);
    float3 L = g_DeferredLightingConsts.m_DirectionalLightVector;
    
    float3 lighting = DefaultLitBxDF(specular, roughness, diffuse, normal, V, L);
    
    // Retrieve the shadow factor from the shadow mask texture
    float shadowFactor = g_ShadowMaskTexture.SampleLevel(g_PointClampSampler, screenUV, 0).r;
    lighting *= shadowFactor;
    
    lighting += emissive;
    
    // Add the ambient term to the lighting result after shadow
    lighting += ambientTerm;
 
    return lighting;
}

[numthreads(8, 8, 1)]
void CS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 screenTexel = dispatchThreadID.xy;
    if (any(screenTexel >= g_DeferredLightingConsts.m_LightingOutputResolution))
    {
        return;
    }
    
    // Convert the texel to screen UV coordinates
    float2 screenUV = (screenTexel + 0.5f) / float2(g_DeferredLightingConsts.m_LightingOutputResolution);
    
    g_LightingOutput[screenTexel] = EvaluteLighting(screenTexel, screenUV);
}

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    outColor = float4(EvaluteLighting(inPosition.xy, inUV), 1.0f);
}

float3 DeferredLightingDebugCommon(uint2 screenTexel)
{
    bool bLightingOnlyOutput = g_DeferredLightingConsts.m_DebugFlags & kDeferredLightingDebugFlag_LightingOnly;
    bool bColorizeInstances = g_DeferredLightingConsts.m_DebugFlags & kDeferredLightingDebugFlag_ColorizeInstances;
    
    float3 rgb = 0.0;
   
    if (bLightingOnlyOutput)
    {
        float3 normal = UnpackOctadehron(g_GBufferNormal[screenTexel].rg);
        float shadowFactor = g_ShadowMaskTexture[screenTexel].r;
        float lightingOnlyShadowFactor = max(0.05f, shadowFactor); // Prevent the shadow factor from being too low to avoid outputting pure black pixels
        rgb = dot(normal, g_DeferredLightingConsts.m_DirectionalLightVector).xxx * lightingOnlyShadowFactor;
    }
    else if (bColorizeInstances)
    {
        float randFloat = g_GBufferAlbedo[screenTexel].w;
        uint seed = randFloat * 4294967296.0f;
        
        float randR = QuickRandomFloat(seed);
        float randG = QuickRandomFloat(seed);
        float randB = QuickRandomFloat(seed);
        rgb = float3(randR, randG, randB);
    }
    
    return rgb;
}

[numthreads(8, 8, 1)]
void CS_Main_Debug(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 screenTexel = dispatchThreadID.xy;
    if (any(screenTexel >= g_DeferredLightingConsts.m_LightingOutputResolution))
    {
        return;
    }
    
    g_LightingOutput[screenTexel] = DeferredLightingDebugCommon(screenTexel);
}

void PS_Main_Debug(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    outColor = float4(DeferredLightingDebugCommon(inPosition.xy), 1.0f);
}
