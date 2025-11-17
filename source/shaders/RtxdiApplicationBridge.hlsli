#ifndef RTXDI_APPLICATION_BRIDGE_HLSLI
#define RTXDI_APPLICATION_BRIDGE_HLSLI

#define RTXDI_ENABLE_PRESAMPLING 0

#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/Utils/Math.hlsli"
#include "lightingcommon.hlsli"

#include "RtxdiShaderInterop.h"

typedef ReSTIRLightInfo RAB_LightInfo;

struct RAB_RandomSamplerState
{
    uint seed;
    uint index;
};

struct RAB_Surface
{
    float3 m_WorldPos;
    float3 m_ViewDir;
    float m_ViewDepth;
    float3 m_Normal;
    float3 m_GeometryNormal;
    float3 m_DiffuseAlbedo;
    float3 m_SpecularF0;
    float m_Roughness;
    float m_DiffuseProbability;
};

struct RAB_LightSample
{
    float3 m_Position;
    float3 m_Radiance;
    float3 m_Normal;
    float m_SolidAnglePdf;
};

// these global resources will be assigned by the Shader itself
static StructuredBuffer<RAB_LightInfo> g_RAB_LightInfoBuffer;
static RaytracingAccelerationStructure g_RAB_SceneTLAS;
static RWStructuredBuffer<RTXDI_PackedDIReservoir> g_RAB_LightReservoirs;
#define RTXDI_LIGHT_RESERVOIR_BUFFER g_RAB_LightReservoirs

float GetSurfaceDiffuseProbability(RAB_Surface surface)
{
    float diffuseWeight = RGBToLuminance(surface.m_DiffuseAlbedo);
    float specularWeight = RGBToLuminance(F_Schlick(surface.m_SpecularF0, dot(surface.m_ViewDir, surface.m_Normal)));
    float sumWeights = diffuseWeight + specularWeight;
    return sumWeights < kKindaSmallNumber ? 1.f : (diffuseWeight / sumWeights);
}

// Translate the light index between the current and previous frame.
// Do nothing as our lights are static
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return int(lightIndex);
}


// Returns an empty RAB_Surface object. It is expected that RAB_IsSurfaceValid returns false when such object is passed to it.
RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.m_ViewDepth = kKindaBigNumber;
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.m_ViewDepth != kKindaBigNumber;
}

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
    return g_RAB_LightInfoBuffer[index];
}

// Returns an empty RAB_LightSample object.
RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

// Returns the solid angle PDF of the light sample.
float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.m_SolidAnglePdf;
}

// Returns true if the light sample comes from an analytic light (e.g. a sphere or rectangle primitive) that cannot be sampled by BRDF rays.
bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return true; // lightSample.lightType != PolymorphicLightType::kTriangle && lightSample.lightType != PolymorphicLightType::kEnvironment;
}

// Returns the direction and distance from the surface to the light sample.
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
    // float diffusePdf = cosTheta / M_PI;
    // float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.m_Material.m_Roughness, kMinRoughness), surface.m_Normal, surface.m_ViewDir, dir);
    // float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.m_DiffuseProbability) : 0.f;
    // return pdf;

    return cosTheta / M_PI; // TODO: proper surface BRDF pdf
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
    lightSample.m_SolidAnglePdf = 1.0f; // TODO: compute solid angle pdf for directional light

    return lightSample;
}

float3 RAB_GetReflectedRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    float3 L = normalize(incomingRadianceLocation - surface.m_WorldPos);
    float3 N = surface.m_Normal;
    float3 V = surface.m_ViewDir;

    if (dot(L, surface.m_GeometryNormal) <= 0)
        return 0;

    float d = Lambert(N, -L);
    float3 s = float3(0, 0, 0);

    // TODO: add specular radiance
    // if (surface.m_Roughness == 0)
    //     s = 0;
    // else
    //     s = GGX_times_NdotL(V, L, N, max(surface.m_Roughness, kMinRoughness), surface.m_SpecularF0);

    return incomingRadiance * (d * surface.m_DiffuseAlbedo + s);
}

float RAB_GetReflectedLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return RTXDI_Luminance(RAB_GetReflectedRadianceForSurface(incomingRadianceLocation, incomingRadiance, surface));
}

// Computes the weight of the given light samples when the given surface is shaded using that light sample.
// Exact or approximate BRDF evaluation can be used to compute the weight.
// ReSTIR will converge to a correct lighting result even if all samples have a fixed weight of 1.0, but that will be very noisy.
// Scaling of the weights can be arbitrary, as long as it's consistent between all lights and surfaces.
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

// Performs importance sampling of the surface's BRDF and returns the sampled direction.
bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    // float3 rand;
    // rand.x = RAB_GetNextRandom(rng);
    // rand.y = RAB_GetNextRandom(rng);
    // rand.z = RAB_GetNextRandom(rng);
    // if (rand.x < surface.m_DiffuseProbability)
    // {
    //     float pdf;
    //     float3 h = SampleCosHemisphere(rand.yz, pdf);
    //     dir = TangentToWorld(surface, h);
    // }
    // else
    // {
    //     float3 Ve = normalize(WorldToTangent(surface, surface.m_ViewDir));
    //     float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.m_Material.m_Roughness, kMinRoughness), Ve, 1.0);
    //     h = normalize(h);
    //     dir = reflect(-surface.m_ViewDir, TangentToWorld(surface, h));
    // }

    // TODO: proper reflected specular BRDF sampling
    float pdf;
    float3 h = SampleCosHemisphere(float2(0,0), pdf);
    dir = TangentToWorld(surface, h);

    return dot(surface.m_Normal, dir) > 0.f;
}

//Returns the world position of the provided surface.
float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.m_WorldPos;
}

float2 RandomFromBarycentric(float3 barycentric)
{
    float sqrtx = 1 - barycentric.x;
    return float2(sqrtx * sqrtx, barycentric.z / sqrtx);
}

float3 HitUVToBarycentric(float2 hitUV)
{
    return float3(1 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);
}

// Traces a ray with the given parameters, looking for a light. If a local light is found, returns true and fills the output parameters with the light sample information.
// If a non-light scene object is hit, returns true and o_lightIndex is set to RTXDI_InvalidLightIndex.
// If nothing is hit, returns false and RTXDI will attempt to do environment map sampling.
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax, out uint o_lightIndex, out float2 o_randXY)
{
    o_lightIndex = RTXDI_InvalidLightIndex;
    o_randXY = 0;

    // RayDesc ray;
    // ray.Origin = origin;
    // ray.Direction = direction;
    // ray.TMin = tMin;
    // ray.TMax = tMax;

    // RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
    // rayQuery.TraceRayInline(g_RAB_SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
    // rayQuery.Proceed();

    // bool hitAnything = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    // if (hitAnything)
    // {
    //     //o_lightIndex = getLightIndex(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex(), rayQuery.CommittedPrimitiveIndex());
    //     o_lightIndex = 0; // TODO: only dir light for now
    //     float2 hitUV = rayQuery.CommittedTriangleBarycentrics();
    //     o_randXY = RandomFromBarycentric(HitUVToBarycentric(hitUV));
    // }

    // return hitAnything;

    return false; // TODO: only dir light for now
}

// Computes the probability of a particular light being sampled from the local light pool with importance sampling, based on the local light PDF texture.
float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 1.0f; // TODO: only dir light for now
}

// Converts a world-space direction into a pair of numbers that, when passed into RAB_SamplePolymorphicLight for the environment light, will make a sample at the same direction.
float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    return float2(0.0f, 0.0f); // TODO: environment light not supported yet
}

// Computes the probability of a particular direction being sampled from the environment map relative to all the other possible directions, based on the environment map PDF texture.
float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    return 0.0f; // TODO: environment light not supported yet
}

// Traces a cheap visibility ray that returns approximate, conservative visibility between the surface and the light sample.
// Conservative means if unsure, assume the light is visible.
// Significant differences between this conservative visibility and the final one will result in more noise.
// This function is used in the spatial resampling functions for ray traced bias correction.
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    float3 L = lightSample.m_Position - surface.m_WorldPos;

    const float kRayOffset = 0.001f;

    RayDesc ray;
    ray.TMin = kRayOffset;
    ray.TMax = max(kRayOffset, length(L) - kRayOffset * 2);
    ray.Direction = normalize(L);
    ray.Origin = surface.m_WorldPos;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    rayQuery.TraceRayInline(g_RAB_SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
    rayQuery.Proceed();

    bool visible = (rayQuery.CommittedStatus() == COMMITTED_NOTHING);

    return visible;
}

#include "Rtxdi/DI/InitialSampling.hlsli"

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI
