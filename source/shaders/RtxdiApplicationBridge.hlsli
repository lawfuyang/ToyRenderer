#ifndef RTXDI_APPLICATION_BRIDGE_HLSLI
#define RTXDI_APPLICATION_BRIDGE_HLSLI

#define RTXDI_ENABLE_PRESAMPLING 0

#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/Utils/Math.hlsli"
#include "lightingcommon.hlsli"

struct RAB_RandomSamplerState
{
    uint seed;
    uint index;
};

struct RAB_Material
{
    float3 m_DiffuseAlbedo;
    float3 m_SpecularF0;
    float m_Roughness;
};

struct RAB_Surface
{
    float3 m_WorldPos;
    float3 m_ViewDir;
    float m_ViewDepth;
    float3 m_Normal;
    float3 m_GeoNormal;
    float m_DiffuseProbability;
    RAB_Material m_Material;
};

// simple directional light only for now
// TODO: pack & compress
struct RAB_LightInfo
{
    float3 m_Direction;
    float3 m_Radiance;
    // TODO: add solid angle for dir light
};

struct RAB_LightSample
{
    float3 m_Position;
    float3 m_Radiance;
    float3 m_Normal;
    float m_SolidAnglePdf;
};

StructuredBuffer<RAB_LightInfo> t_LightDataBuffer : register(t0);
RWStructuredBuffer<RTXDI_PackedDIReservoir> u_LightReservoirs : register(u0);

#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs

// Initialized the random sampler for a given pixel or tile index.
// The 'rngSequenceOffset' parameter is provided to help generate different RNG sequences for different resampling passes, which is important for image quality.
// In general, a high quality RNG is critical to get good results from ReSTIR.
// A table-based blue noise RNG dose not provide enough entropy, for example.
RAB_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint rngSequenceOffset)
{
    RAB_RandomSamplerState state;

    uint linearPixelIndex = RTXDI_ZCurveToLinearIndex(index);

    state.index = 1;
    state.seed = RTXDI_JenkinsHash(linearPixelIndex) + rngSequenceOffset;

    return state;
}

// Draws a random number X from the sampler, so that (0 <= X < 1).
float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    return SampleUniformRng(rng.seed, rng.index);
}

// Load the packed light information from the buffer.
// Ignore the previousFrame parameter as all lights are always loaded
RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return t_LightDataBuffer[index];
}

RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.m_SolidAnglePdf;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return true; // lightSample.lightType != PolymorphicLightType::kTriangle && lightSample.lightType != PolymorphicLightType::kEnvironment;
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 o_lightDir, out float o_lightDistance)
{
    // TODO: only directional light for now
    // if (lightSample.lightType == PolymorphicLightType::kEnvironment)
    // {
    //     o_lightDir = -lightSample.m_Normal;
    //     o_lightDistance = DISTANT_LIGHT_DISTANCE;
    // }
    // else
    // {
        float3 toLight = lightSample.m_Position - surface.m_WorldPos;
        o_lightDistance = length(toLight);
        o_lightDir = toLight / o_lightDistance;
    // }
}

// Return PDF wrt solid angle for the BRDF in the given dir
float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.m_Normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.m_Material.m_Roughness, kMinRoughness), surface.m_Normal, surface.m_ViewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.m_DiffuseProbability) : 0.f;
    return pdf;
}

// Samples a polymorphic light relative to the given receiver surface.
// For most light types, the "uv" parameter is just a pair of uniform random numbers, originally produced by the RAB_GetNextRandom function and then stored in light reservoirs.
// For importance sampled environment lights, the "uv" parameter has the texture coordinates
// in the PDF texture, normalized to the (0..1) range.
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    // PolymorphicLightSample pls = PolymorphicLight::calcSample(lightInfo, uv, surface.worldPos);

    // RAB_LightSample lightSample;
    // lightSample.position = pls.position;
    // lightSample.normal = pls.normal;
    // lightSample.radiance = pls.radiance;
    // lightSample.solidAnglePdf = pls.solidAnglePdf;
    // lightSample.lightType = getLightType(lightInfo);
    // return lightSample;

    RAB_LightSample lightSample;
    lightSample.m_Position = surface.m_WorldPos + (-lightInfo.m_Direction * kKindaBigNumber);
    lightSample.m_Normal = lightInfo.m_Direction;
    lightSample.m_Radiance = lightInfo.m_Radiance;
    lightSample.m_SolidAnglePdf = 0.0f; // TODO: compute solid angle pdf for directional light

    return lightSample;
}

float3 RAB_GetReflectedRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    float3 L = normalize(incomingRadianceLocation - surface.m_WorldPos);
    float3 N = surface.m_Normal;
    float3 V = surface.m_ViewDir;

    if (dot(L, surface.m_GeoNormal) <= 0)
        return 0;

    float d = Lambert(N, -L);
    float3 s;
    if (surface.m_Material.m_Roughness == 0)
        s = 0;
    else
        s = GGX_times_NdotL(V, L, N, max(surface.m_Material.m_Roughness, kMinRoughness), surface.m_Material.m_SpecularF0);

    return incomingRadiance * (d * surface.m_Material.m_DiffuseAlbedo + s);
}

float RAB_GetReflectedLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return RTXDI_Luminance(RAB_GetReflectedRadianceForSurface(incomingRadianceLocation, incomingRadiance, surface));
}

// Computes the weight of the given light samples when the given surface is
// shaded using that light sample. Exact or approximate BRDF evaluation can be
// used to compute the weight. ReSTIR will converge to a correct lighting result
// even if all samples have a fixed weight of 1.0, but that will be very noisy.
// Scaling of the weights can be arbitrary, as long as it's consistent
// between all lights and surfaces.
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.m_SolidAnglePdf <= 0)
        return 0;
    
    return RAB_GetReflectedLuminanceForSurface(lightSample.m_Position, lightSample.m_Radiance, surface) / lightSample.m_SolidAnglePdf;
}

float3 WorldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.m_Normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.m_Normal, w));
}

float3 TangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.m_Normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.m_Normal * h.z;
}

bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RAB_GetNextRandom(rng);
    rand.y = RAB_GetNextRandom(rng);
    rand.z = RAB_GetNextRandom(rng);
    if (rand.x < surface.m_DiffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = TangentToWorld(surface, h);
    }
    else
    {
        float3 Ve = normalize(WorldToTangent(surface, surface.m_ViewDir));
        float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.m_Material.m_Roughness, kMinRoughness), Ve, 1.0);
        h = normalize(h);
        dir = reflect(-surface.m_ViewDir, TangentToWorld(surface, h));
    }

    return dot(surface.m_Normal, dir) > 0.f;
}

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI
