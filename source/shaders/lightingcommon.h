#ifndef _LIGHTING_COMMON_H_
#define _LIGHTING_COMMON_H_

#include "common.h"

struct GBufferParams
{
    float3 m_Albedo;
    float m_Alpha;
    float3 m_Normal;
    float m_Occlusion;
    float m_Roughness;
    float m_Metallic;
};

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

// Diffuse BRDF: Lambertian Diffuse
float3 Diffuse_Lambert(float3 diffuseColor)
{
    return diffuseColor * (1.0f / M_PI);
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

float3 DefaultLitBxDF(float3 specularColor, float specularRoughness, float3 diffuseColor, float3 N, float3 V, float3 L)
{
    // Diffuse BRDF
    float3 lighting = Diffuse_Lambert(diffuseColor);

	float NdotL = saturate(dot(N, L));

	float3 H = normalize(V + L);
	float NdotV = saturate(abs(dot(N, V)) + 1e-5); // Bias to avoid artifacting
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	// Generalized microfacet Specular BRDF
	float a = specularRoughness * specularRoughness;
	float a2 = clamp(a * a, 0.0001f, 1.0f);
	float D = D_GGX(a2, NdotH);
	float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
	float3 F = F_Schlick(specularColor, VdotH);
	lighting += (D * Vis) * F;

    return lighting * NdotL;
}

float3 AmbientTerm(Texture2D<uint> SSAOTexture, uint2 texel, float3 diffuseColor)
{
    float3 result = 0.0f;

    float SSAOVisibility = SSAOTexture[texel] / 255.0f;

    // TODO: AMD Brixelizer GI
    const float kHardcodedAmbient = 0.1f * SSAOVisibility;
    result += kHardcodedAmbient.xxx;

    result *= Diffuse_Lambert(diffuseColor);

    return result;
}

#endif // _LIGHTING_COMMON_H_
