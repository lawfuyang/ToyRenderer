#include "toyrenderer_common.hlsli"

#include "shadowfiltering.hlsl"
#include "lightingcommon.hlsli"
#include "random.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

cbuffer g_DeferredLightingPassConstantsBuffer : register(b0) { DeferredLightingConsts g_DeferredLightingConsts; }
Texture2D<uint4> g_GBufferA : register(t0);
Texture2D g_GBufferMotion : register(t1);
Texture2D g_DepthBuffer : register(t2);
Texture2D<uint> g_SSAOTexture : register(t3);
Texture2D g_ShadowMaskTexture : register(t4);
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t5);
Texture2DArray<float4> g_ProbeData : register(t6);
Texture2DArray<float4> g_ProbeIrradiance : register(t7);
Texture2DArray<float4> g_ProbeDistance : register(t8);
RWTexture2D<float3> g_LightingOutput : register(u0);
SamplerState g_PointClampSampler : register(s0);
SamplerState g_LinearWrapSampler : register(s1);

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[inPosition.xy], gbufferParams);
    
    // Convert screen UV coordinates and depth to world position
    float depth = g_DepthBuffer[inPosition.xy].x;
    float3 worldPosition = ScreenUVToWorldPosition(inUV, depth, g_DeferredLightingConsts.m_ClipToWorld);
    
    float3 lighting = EvaluateDirectionalLight(gbufferParams, g_DeferredLightingConsts.m_CameraOrigin, worldPosition, g_DeferredLightingConsts.m_DirectionalLightVector);
    
    // Retrieve the shadow factor from the shadow mask texture
    float shadowFactor = g_ShadowMaskTexture.SampleLevel(g_PointClampSampler, inUV, 0).r;
    lighting *= shadowFactor;
    
    lighting += gbufferParams.m_Emissive;
    
    // TODO: multiple volumes
    uint volumeIndex = 0;
    DDGIVolumeDescGPU DDGIVolumeDesc = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);
    
    DDGIVolumeResources volumeResources;
    volumeResources.probeIrradiance = g_ProbeIrradiance;
    volumeResources.probeDistance = g_ProbeDistance;
    volumeResources.probeData = g_ProbeData;
    volumeResources.bilinearSampler = g_LinearWrapSampler;
    
    GetDDGIIrradianceArguments irradianceArgs;
    irradianceArgs.m_WorldPosition = worldPosition;
    irradianceArgs.m_VolumeDesc = DDGIVolumeDesc;
    irradianceArgs.m_Normal = gbufferParams.m_Normal;
    irradianceArgs.m_ViewDirection = normalize(worldPosition - g_DeferredLightingConsts.m_CameraOrigin);
    irradianceArgs.m_DDGIVolumeResources = volumeResources;
    
    float3 irradiance = Diffuse_Lambert(gbufferParams.m_Albedo.rgb) * GetDDGIIrradiance(irradianceArgs);
    
    if (g_DeferredLightingConsts.m_SSAOEnabled)
    {
        irradiance *= g_SSAOTexture[inPosition.xy] / 255.0f;
    }
    
    lighting += irradiance;
   
    outColor = float4(lighting, 1.0f);
}

void PS_Main_Debug(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[inPosition.xy], g_GBufferMotion[inPosition.xy], gbufferParams);
    
    float3 worldPosition = ScreenUVToWorldPosition(inUV, g_DepthBuffer[inPosition.xy].x, g_DeferredLightingConsts.m_ClipToWorld);
    
    float shadowFactor = g_ShadowMaskTexture.SampleLevel(g_PointClampSampler, inUV, 0).r;
    shadowFactor = max(0.05f, shadowFactor); // Prevent the shadow factor from being too low to avoid outputting pure black pixels
    
    float3 rgb = float3(0, 0, 0);
    
    if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_LightingOnly)
    {
        rgb = dot(gbufferParams.m_Normal, g_DeferredLightingConsts.m_DirectionalLightVector).xxx * shadowFactor;
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
        // TODO: multiple volumes
        uint volumeIndex = 0;
        DDGIVolumeDescGPU DDGIVolumeDesc = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);
    
        DDGIVolumeResources volumeResources;
        volumeResources.probeIrradiance = g_ProbeIrradiance;
        volumeResources.probeDistance = g_ProbeDistance;
        volumeResources.probeData = g_ProbeData;
        volumeResources.bilinearSampler = g_LinearWrapSampler;
    
        GetDDGIIrradianceArguments irradianceArgs;
        irradianceArgs.m_WorldPosition = worldPosition;
        irradianceArgs.m_VolumeDesc = DDGIVolumeDesc;
        irradianceArgs.m_Normal = gbufferParams.m_Normal;
        irradianceArgs.m_ViewDirection = normalize(worldPosition - g_DeferredLightingConsts.m_CameraOrigin);
        irradianceArgs.m_DDGIVolumeResources = volumeResources;
        
        rgb = GetDDGIIrradiance(irradianceArgs);
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
    else if (g_DeferredLightingConsts.m_DebugMode == kDeferredLightingDebugMode_MotionVectors)
    {
        rgb.xy = gbufferParams.m_Motion / g_DeferredLightingConsts.m_LightingOutputResolution;
    }
    
    outColor = float4(rgb, 1.0f);
}
