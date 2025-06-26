#pragma once

#include "toyrenderer_common.hlsli"

#include "DDGIShaderConfig.h"
#include "../Irradiance.hlsl"

#include "fastmath.hlsli"
#include "packunpack.hlsli"

#include "ShaderInterop.h"

struct GBufferParams
{
    float4 m_Albedo;
    float3 m_Emissive;
    float3 m_Normal;
    float m_Roughness;
    float m_Metallic;
    float2 m_Motion;
    float m_DebugValue;
    float m_AlphaCutoff;
};

void PackGBuffer(in GBufferParams gbufferParams, out uint4 packedGBufferA)
{
    packedGBufferA.x = PackRGBA8(float4(gbufferParams.m_Albedo.rgb, gbufferParams.m_DebugValue));
    packedGBufferA.y = PackUnorm2x16(PackOctadehron(gbufferParams.m_Normal));
    packedGBufferA.z = PackR9G9B9E5(gbufferParams.m_Emissive);
    packedGBufferA.w = PackRGBA8(float4(gbufferParams.m_Roughness, gbufferParams.m_Metallic, 0.0f, 0.0f));
}

void UnpackGBuffer(in uint4 packedGBufferA, out GBufferParams gbufferParams)
{
    float4 unpackedGBufferA_X = UnpackRGBA8(packedGBufferA.x);
    float3 unpackedGBufferA_Y = UnpackOctadehron(UnpackUnorm2x16(packedGBufferA.y));
    float3 unpackedGBufferA_Z = UnpackR9G9B9E5(packedGBufferA.z);
    float4 unpackedGBufferA_W = UnpackRGBA8(packedGBufferA.w);
    
    gbufferParams.m_Albedo.rgb = unpackedGBufferA_X.rgb;
    gbufferParams.m_Albedo.a = 1.0f;
    gbufferParams.m_DebugValue = unpackedGBufferA_X.w;
    gbufferParams.m_Normal = unpackedGBufferA_Y;
    gbufferParams.m_Emissive = unpackedGBufferA_Z;
    gbufferParams.m_Roughness = unpackedGBufferA_W.x;
    gbufferParams.m_Metallic = unpackedGBufferA_W.y;
    gbufferParams.m_Motion = float2(0.0f, 0.0f);
}

void UnpackGBuffer(in uint4 packedGBufferA, float4 GBufferMotion, out GBufferParams gbufferParams)
{
    UnpackGBuffer(packedGBufferA, gbufferParams);
    gbufferParams.m_Motion = GBufferMotion.xy;
}

// 0.08 is a max F0 we define for dielectrics which matches with Crystalware and gems (0.05 - 0.08)
// This means we cannot represent Diamond-like surfaces as they have an F0 of 0.1 - 0.2
float DielectricSpecularToF0(float specular)
{
    return 0.08f * specular;
}

//Note from Filament: vec3 f0 = 0.16 * reflectance * reflectance * (1.0 - metallic) + baseColor * metallic;
// F0 is the base specular reflectance of a surface
// For dielectrics, this is monochromatic commonly between 0.02 (water) and 0.08 (gems) and derived from a separate specular value
// For conductors, this is based on the base color we provided
float3 ComputeF0(float specular, float3 baseColor, float metalness)
{
    return lerp(DielectricSpecularToF0(specular).xxx, baseColor, metalness);
}

float3 ComputeDiffuseColor(float3 baseColor, float metalness)
{
    return baseColor * (1.0f - metalness);
}

float3 Diffuse_Lambert(float3 diffuseColor)
{
    return diffuseColor * (1.0f / M_PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
    float FdV = 1 + (FD90 - 1) * Pow5(1 - NoV);
    float FdL = 1 + (FD90 - 1) * Pow5(1 - NoL);
    return DiffuseColor * ((1 / M_PI) * FdV * FdL);
}

// [ Chan 2018, "Material Advances in Call of Duty: WWII" ]
// It has been extended here to fade out retro reflectivity contribution from area light in order to avoid visual artefacts.
float3 Diffuse_Chan(float3 DiffuseColor, float a2, float NoV, float NoL, float VoH, float NoH, float RetroReflectivityWeight)
{
	// We saturate each input to avoid out of range negative values which would result in weird darkening at the edge of meshes (resulting from tangent space interpolation).
    NoV = saturate(NoV);
    NoL = saturate(NoL);
    VoH = saturate(VoH);
    NoH = saturate(NoH);

	// a2 = 2 / ( 1 + exp2( 18 * g )
    float g = saturate((1.0 / 18.0) * log2(2 * fastRcpSqrtNR0(a2) - 1));

    float F0 = VoH + Pow5(1 - VoH);
    float FdV = 1 - 0.75 * Pow5(1 - NoV);
    float FdL = 1 - 0.75 * Pow5(1 - NoL);

	// Rough (F0) to smooth (FdV * FdL) response interpolation
    float Fd = lerp(F0, FdV * FdL, saturate(2.2 * g - 0.5));

	// Retro reflectivity contribution.
    float Fb = ((34.5 * g - 59) * g + 24.5) * VoH * exp2(-max(73.2 * g - 21.2, 8.9) * fastSqrtNR0(NoH));
	// It fades out when lights become area lights in order to avoid visual artefacts.
    Fb *= RetroReflectivityWeight;
	
    return DiffuseColor * ((1 / M_PI) * (Fd + Fb));
}

// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
float3 EnvBRDFApprox(float3 specularColor, float roughness, float ndotv)
{
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

// GGX / Trowbridge-Reitz
// Note the division by PI here
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NdotH)
{
    float d = (NdotH * a2 - NdotH) * NdotH + 1;
    return a2 / (M_PI * d * d);
}

// Appoximation of joint Smith term for GGX
// Returned value is G2 / (4 * NdotL * NdotV). So predivided by specular BRDF denominator
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NdotV, float NdotL)
{
    float Vis_SmithV = NdotL * (NdotV * (1 - a2) + a2);
    float Vis_SmithL = NdotV * (NdotL * (1 - a2) + a2);
    return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 f0, float VdotH)
{
    float Fc = Pow5(1.0f - VdotH);
    return Fc + (1.0f - Fc) * f0;
}

float3 DefaultLitBxDF(float3 specularColor, float roughness, float3 albedo, float3 N, float3 V, float3 L)
{
    float3 H = normalize(V + L);
    float NdotV = saturate(abs(dot(N, V)) + 1e-5); // Bias to avoid artifacting
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float LdotH = saturate(dot(L, H));
    
    // Diffuse BRDF
    float3 diffuse;
    diffuse = Diffuse_Lambert(albedo);
    //diffuse = Diffuse_Burley(albedo, roughness, NdotV, NdotL, VdotH);
    //diffuse = Diffuse_Chan(albedo, roughness, NdotV, NdotL, VdotH, NdotH, 0.0f);
    
	// Generalized microfacet Specular BRDF
    float3 specular;
    float a = roughness * roughness;
	float a2 = clamp(a * a, 0.0001f, 1.0f);
	float D = D_GGX(a2, NdotH);
	float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
	float3 F = F_Schlick(specularColor, VdotH);
    specular = (D * Vis) * F;
    
    specular += EnvBRDFApprox(specularColor, roughness, NdotV);

    return (diffuse + specular) * NdotL;
}

float3 EvaluateDirectionalLight(
    GBufferParams gbufferParams,
    float3 origin,
    float3 worldPosition,
    float3 lightDirection,
    float lightStrength)
{
    const float materialSpecular = 0.5f; // TODO?
    float3 diffuse = ComputeDiffuseColor(gbufferParams.m_Albedo.rgb, gbufferParams.m_Metallic);
    float3 specular = ComputeF0(materialSpecular, gbufferParams.m_Albedo.rgb, gbufferParams.m_Metallic);
    
    float3 V = normalize(origin - worldPosition);
    float3 L = lightDirection;
    
    return DefaultLitBxDF(specular, gbufferParams.m_Roughness, diffuse, gbufferParams.m_Normal, V, L) * lightStrength;
}

struct SampleMaterialValueArguments
{
    float2 m_TexCoord;
    uint m_MaterialFlag; // preferably compile-time const
    MaterialData m_MaterialData;
    sampler m_Samplers[SamplerIdx_Count];
    float4 m_DefaultValue; // preferably compile-time const
    float m_OveriddenSampleLevel; // preferably compile-time const. Set to a negative value to use hardware mip level
};

float4 SampleMaterialValue(SampleMaterialValueArguments inArgs)
{
    float2 texCoord = inArgs.m_TexCoord;
    uint materialFlag = inArgs.m_MaterialFlag;
    MaterialData materialData = inArgs.m_MaterialData;
    sampler samplers[SamplerIdx_Count] = inArgs.m_Samplers;
    float4 defaultValue = inArgs.m_DefaultValue;
    float overridenSampleLevel = inArgs.m_OveriddenSampleLevel;
    
    if (!(materialData.m_MaterialFlags & materialFlag))
    {
        return defaultValue;
    }
    
    uint textureSamplerAndDescriptorIndex;
    switch (materialFlag)
    {
        case MaterialFlag_UseDiffuseTexture:
            textureSamplerAndDescriptorIndex = materialData.m_AlbedoTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseNormalTexture:
            textureSamplerAndDescriptorIndex = materialData.m_NormalTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseMetallicRoughnessTexture:
            textureSamplerAndDescriptorIndex = materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex;
            break;
        case MaterialFlag_UseEmissiveTexture:
            textureSamplerAndDescriptorIndex = materialData.m_EmissiveTextureSamplerAndDescriptorIndex;
            break;
    }
        
    uint texIdx = NonUniformResourceIndex(textureSamplerAndDescriptorIndex & 0x3FFFFFFF);
    Texture2D materialTexture = ResourceDescriptorHeap[texIdx];

    uint samplerIdx = textureSamplerAndDescriptorIndex >> 30;
    sampler materialSampler = samplers[samplerIdx];
        
    // TODO: use 'SampleGrad' for appropriate mip level
    if (overridenSampleLevel >= 0.0f)
    {
        return materialTexture.SampleLevel(materialSampler, texCoord, overridenSampleLevel);
    }
    else
    {
        return materialTexture.Sample(materialSampler, texCoord);
    }
}

struct GetCommonGBufferParamsArguments
{
    float2 m_TexCoord;
    float3 m_WorldPosition;
    float3 m_Normal;
    MaterialData m_MaterialData;
    sampler m_Samplers[SamplerIdx_Count];
};

GBufferParams GetCommonGBufferParams(GetCommonGBufferParamsArguments inArgs)
{
    float2 texCoord = inArgs.m_TexCoord;
    float3 worldPosition = inArgs.m_WorldPosition;
    float3 normal = inArgs.m_Normal;
    MaterialData materialData = inArgs.m_MaterialData;
    sampler samplers[SamplerIdx_Count] = inArgs.m_Samplers;

    GBufferParams result = (GBufferParams) 0;
    
    SampleMaterialValueArguments sampleArgs;
    sampleArgs.m_TexCoord = texCoord;
    sampleArgs.m_MaterialData = materialData;
    sampleArgs.m_Samplers = samplers;
    sampleArgs.m_OveriddenSampleLevel = -1.0f;
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseDiffuseTexture;
    sampleArgs.m_DefaultValue = float4(1, 1, 1, 1);
    float4 albedoSample = SampleMaterialValue(sampleArgs);
    result.m_Albedo = materialData.m_ConstAlbedo * albedoSample;
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseNormalTexture;
    sampleArgs.m_DefaultValue = float4(0.5f, 0.5f, 1.0f, 0.0f);
    float4 normalSample = SampleMaterialValue(sampleArgs);
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseMetallicRoughnessTexture;
    sampleArgs.m_DefaultValue = float4(0.0f, 1.0f, 0.0f, 0.0f);
    float4 metalRoughnessSample = SampleMaterialValue(sampleArgs);
    
    sampleArgs.m_MaterialFlag = MaterialFlag_UseEmissiveTexture;
    sampleArgs.m_DefaultValue = float4(1, 1, 1, 0);
    float4 emissiveSample = SampleMaterialValue(sampleArgs);
    
    result.m_Roughness = metalRoughnessSample.g;
    result.m_Metallic = metalRoughnessSample.b;
    result.m_Emissive = materialData.m_ConstEmissive * emissiveSample.rgb;
    
    result.m_Normal = normal;
    if (materialData.m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        float3 unpackedNormal = TwoChannelNormalX2(normalSample.xy);
        float3x3 TBN = CalculateTBNWithoutTangent(worldPosition, normal, texCoord);
        result.m_Normal = normalize(mul(unpackedNormal, TBN));
    }
    
    result.m_AlphaCutoff = materialData.m_AlphaCutoff;
    
    return result;
}

struct GetDDGIIrradianceArguments
{
    float3 m_WorldPosition;
    DDGIVolumeDescGPU m_VolumeDesc;
    float3 m_Normal;
    float3 m_ViewDirection;
    DDGIVolumeResources m_DDGIVolumeResources;
};

float3 GetDDGIIrradiance(GetDDGIIrradianceArguments args)
{
    float3 worldPosition = args.m_WorldPosition;
    DDGIVolumeDescGPU volumeDesc = args.m_VolumeDesc;
    float3 surfaceNormal = args.m_Normal;
    float3 viewDirection = args.m_ViewDirection;
    DDGIVolumeResources volumeResources = args.m_DDGIVolumeResources;
    
    float3 irradiance = float3(0, 0, 0);
    
    float volumeBlendWeight = DDGIGetVolumeBlendWeight(worldPosition, volumeDesc);
    if (volumeBlendWeight > 0.0f)
    {
        float3 surfaceBias = DDGIGetSurfaceBias(surfaceNormal, viewDirection, volumeDesc);
    
        float3 samplingDirection = surfaceNormal;
        irradiance = DDGIGetVolumeIrradiance(worldPosition, surfaceBias, samplingDirection, volumeDesc, volumeResources);
        irradiance *= volumeBlendWeight;
    }
    
    return irradiance;
}
