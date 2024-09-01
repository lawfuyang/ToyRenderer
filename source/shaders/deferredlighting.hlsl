#include "common.h"

#include "shadowfiltering.hlsl"
#include "lightingcommon.h"
#include "hzb.h"
#include "random.h"

#include "shared/CommonConsts.h"
#include "shared/DeferredLightingStructs.h"
#include "shared/IndirectArguments.h"

cbuffer g_DeferredLightingPassConstantsBuffer : register(b0) { DeferredLightingConsts g_DeferredLightingConsts; }
Texture2D g_GBufferAlbedo : register(t0);
Texture2D g_GBufferNormal : register(t1);
Texture2D g_GBufferPBR : register(t2);
Texture2D g_DepthBuffer : register(t3);
Texture2D<uint> g_SSAOTexture : register(t4);
Texture2D g_ShadowMaskTexture : register(t5);
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
    float3 ambientTerm = AmbientTerm(g_SSAOTexture, g_DeferredLightingConsts.m_SSAOEnabled ? screenTexel : uint2(0, 0), albedo);
    
    // If the tile is fully shadowed, return the lighting result with only ambient
    if (g_DeferredLightingConsts.m_TileID == Tile_ID_Full_Shadow)
    {
        return ambientTerm;
    }
    
    // Convert screen UV coordinates and depth to world position
    float3 worldPosition = ScreenUVToWorldPosition(screenUV, depth, g_DeferredLightingConsts.m_InvViewProjMatrix);
    
    // Retrieve the PBR values from the G-buffer
    float3 pbr = g_GBufferPBR[screenTexel].rgb;
    float occlusion = pbr.x;
    float roughness = pbr.y;
    float metallic = pbr.z;
    
    // 
    const float materialSpecular = 0.5f; // TODO?
    float3 diffuse = ComputeDiffuseColor(albedo, metallic);
    float3 specular = ComputeF0(materialSpecular, albedo, metallic);
    float3 normal = DecodeNormal(g_GBufferNormal[screenTexel].rg);
    
    float3 V = normalize(g_DeferredLightingConsts.m_CameraOrigin - worldPosition);
    float3 L = g_DeferredLightingConsts.m_DirectionalLightVector;
    
    float3 lighting = DefaultLitBxDF(specular, roughness, diffuse, normal, V, L);
    
    // Retrieve the shadow factor from the shadow mask texture
    float shadowFactor = g_ShadowMaskTexture[screenTexel].r;
    lighting *= shadowFactor;
    
    // Add the ambient term to the lighting result after shadow
    lighting += ambientTerm;
 
    return lighting;
}

[numthreads(kDeferredLightingTileSize * kDeferredLightingTileSize, 1, 1)]
void CS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint nbTotalScreenTiles = g_DeferredLightingConsts.m_NbTiles.x * g_DeferredLightingConsts.m_NbTiles.y;
    
    // If the thread is outside the tile count, return
    if (groupId.x >= nbTotalScreenTiles)
    {
        return;
    }
    
    // Retrieve the tile offset and screen texel
    uint2 tileTexel = uint2(groupThreadID.x % kDeferredLightingTileSize, groupThreadID.x / kDeferredLightingTileSize);
    uint currentTileIDBaseOffset = nbTotalScreenTiles * g_DeferredLightingConsts.m_TileID;
    uint2 tileOffset = g_TileOffsets[currentTileIDBaseOffset + groupId.x];
    uint2 screenTexel = tileOffset + tileTexel;
    
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

void PS_Main_Debug(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    bool bLightingOnlyOutput = g_DeferredLightingConsts.m_DebugFlags & kDeferredLightingDebugFlag_LightingOnly;
    bool bColorizeInstances = g_DeferredLightingConsts.m_DebugFlags & kDeferredLightingDebugFlag_ColorizeInstances;
    
    float3 rgb = 0.0;
   
    if (bLightingOnlyOutput)
    {
        float3 normal = DecodeNormal(g_GBufferNormal[inPosition.xy].rg);
        float shadowFactor = g_ShadowMaskTexture[inPosition.xy].r;
        float lightingOnlyShadowFactor = max(0.05f, shadowFactor); // Prevent the shadow factor from being too low to avoid outputting pure black pixels
        rgb = dot(normal, g_DeferredLightingConsts.m_DirectionalLightVector).xxx * lightingOnlyShadowFactor;
    }
    else if (bColorizeInstances)
    {
        float randFloat = g_GBufferAlbedo[inPosition.xy].w;
        uint seed = randFloat * 4294967296.0f;
        
        float randR = QuickRandomFloat(seed);
        float randG = QuickRandomFloat(seed);
        float randB = QuickRandomFloat(seed);
        rgb = float3(randR, randG, randB);
    }
    
    outColor = float4(rgb, 1.0f);
}

cbuffer g_DeferredLightingTileClassificationConstsConstantsBuffer : register(b0) { DeferredLightingTileClassificationConsts g_DeferredLightingTileClassificationConsts; }
Texture2D g_ConservativeShadowMask : register(t0);
RWStructuredBuffer<uint> g_TileCounter : register(u0);
RWStructuredBuffer<DispatchIndirectArguments> g_DispatchIndirectArgs : register(u1);
RWStructuredBuffer<DrawIndirectArguments> g_DrawIndirectArgs : register(u2);
RWStructuredBuffer<uint2> g_TileOffsetsOut : register(u3);
sampler g_PointClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CS_TileClassification(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 nbTiles = g_DeferredLightingTileClassificationConsts.m_NbTiles;
    
    // If the thread is outside the tile count, return
    if (any(dispatchThreadID.xy >= nbTiles))
    {
        return;
    }
    
    float2 screenUV = (dispatchThreadID.xy + 0.5f) / nbTiles;
    
    uint tileID = Tile_ID_Normal;
    
    bool bFullyShadowedTile = (g_ConservativeShadowMask.SampleLevel(g_PointClampSampler, screenUV, 0).x == 0.0f);
    if (bFullyShadowedTile)
    {
        tileID = Tile_ID_Full_Shadow;
    }
    
    // Increment the tile counter and store the tile offset
    uint tileIdx;
    InterlockedAdd(g_TileCounter[tileID], 1, tileIdx);
    
    uint currentTileIDBaseOffset = nbTiles.x * nbTiles.y * tileID;
    g_TileOffsetsOut[currentTileIDBaseOffset + tileIdx] = dispatchThreadID.xy * kDeferredLightingTileSize;
    
    // increment tile count for CS codepath
    InterlockedAdd(g_DispatchIndirectArgs[tileID].m_ThreadGroupCountX, 1);
    
    // increment instance count for VS/PS codepath
    InterlockedAdd(g_DrawIndirectArgs[tileID].m_InstanceCount, 1);
}

cbuffer g_DeferredLightingTileClassificationDebugConstsConstantsBuffer : register(b0) { DeferredLightingTileClassificationDebugConsts g_DeferredLightingTileClassificationDebugConsts; }
RWTexture2D<float3> g_DebugOutput : register(u0);
Texture2D g_DebugDepthBuffer : register(t0);
StructuredBuffer<uint2> g_DebugTileOffsets : register(t99);

float3 TileClassificationCommon()
{
    float3 overlayColor = g_DeferredLightingTileClassificationDebugConsts.m_OverlayColor;
    
    // prevent adapt luminance from being applied
    overlayColor *= g_DeferredLightingTileClassificationDebugConsts.m_SceneLuminance;
    
    return overlayColor;
}

[numthreads(kDeferredLightingTileSize * kDeferredLightingTileSize, 1, 1)]
void CS_TileClassificationDebug(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 tileTexel = uint2(groupThreadID.x % kDeferredLightingTileSize, groupThreadID.x / kDeferredLightingTileSize);
    
    // If the tile texel is on the border, return (to visualize tiles properly)
    //if (tileTexel.x == 0 || tileTexel.x == (kDeferredLightingTileSize - 1) || tileTexel.y == 0 || tileTexel.y == (kDeferredLightingTileSize - 1))
    //{
    //    return;
    //}
    
    uint currentTileIDBaseOffset = g_DeferredLightingTileClassificationDebugConsts.m_NbTiles * g_DeferredLightingTileClassificationDebugConsts.m_TileID;
    uint2 tileOffset = g_DebugTileOffsets[currentTileIDBaseOffset + groupId.x];
    uint2 screenTexel = tileOffset + tileTexel;
    
    // replicate sky stencil in VS/PS code path, equivalent to far depth
    if (g_DebugDepthBuffer[screenTexel].x == kFarDepth)
    {
        return;
    };
    
    g_DebugOutput[screenTexel] += TileClassificationCommon();
}

void PS_TileClassificationDebug(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    outColor = float4(TileClassificationCommon(), 1.0f);
}
