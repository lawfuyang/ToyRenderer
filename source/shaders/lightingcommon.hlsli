#ifndef _LIGHTING_COMMON_H_
#define _LIGHTING_COMMON_H_

#include "common.hlsli"
#include "fastmath.hlsli"

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
    
    // TODO: AMD Brixelizer GI
    float3 envSpecularColor = EnvBRDFApprox(specularColor, a, NdotV);
    specular += envSpecularColor;

    return (diffuse + specular) * NdotL;
}

float3 BasilicaSHIrradiance(float3 nrm)
{
    struct SHCoefficients
    {
        float3 l00, l1m1, l10, l11, l2m2, l2m1, l20, l21, l22;
    };

    // St. Peter's Basilica SH
    // https://www.shadertoy.com/view/lt2GRD
    const SHCoefficients SH_STPETER =
    {
        float3(0.3623915, 0.2624130, 0.2326261),
		float3(0.1759131, 0.1436266, 0.1260569),
		float3(-0.0247311, -0.0101254, -0.0010745),
		float3(0.0346500, 0.0223184, 0.0101350),
		float3(0.0198140, 0.0144073, 0.0043987),
		float3(-0.0469596, -0.0254485, -0.0117786),
		float3(-0.0898667, -0.0760911, -0.0740964),
		float3(0.0050194, 0.0038841, 0.0001374),
		float3(-0.0818750, -0.0321501, 0.0033399)
    };
    const SHCoefficients c = SH_STPETER;
    const float c1 = 0.429043;
    const float c2 = 0.511664;
    const float c3 = 0.743125;
    const float c4 = 0.886227;
    const float c5 = 0.247708;
    return(
		c1 * c.l22 * (nrm.x * nrm.x - nrm.y * nrm.y) +
		c3 * c.l20 * nrm.z * nrm.z +
		c4 * c.l00 -
		c5 * c.l20 +
		2.0 * c1 * c.l2m2 * nrm.x * nrm.y +
		2.0 * c1 * c.l21 * nrm.x * nrm.z +
		2.0 * c1 * c.l2m1 * nrm.y * nrm.z +
		2.0 * c2 * c.l11 * nrm.x +
		2.0 * c2 * c.l1m1 * nrm.y +
		2.0 * c2 * c.l10 * nrm.z
		);
}

float3 AmbientTerm(Texture2D<uint> SSAOTexture, uint2 texel, float3 diffuseColor, float3 normal)
{
    float3 result = 0.0f;

    // TODO: AMD Brixelizer GI 
    result += BasilicaSHIrradiance(normal);

    result *= Diffuse_Lambert(diffuseColor);
    
    float SSAOVisibility = SSAOTexture[texel] / 255.0f;
    result *= SSAOVisibility;

    return result;
}

#endif // _LIGHTING_COMMON_H_
