#include "toyrenderer_common.hlsli"

#include "RtxdiApplicationBridge.hlsli"

cbuffer g_ReSTIRLightingConstantsBuffer : register(b0) { ReSTIRLightingConstants g_ReSTIRLightingConstants; }
RaytracingAccelerationStructure g_SceneTLAS : register(t0);
StructuredBuffer<RAB_LightInfo> g_LightInfoBuffer : register(t1);
Texture2D<uint4> g_GBufferA : register(t2);
Texture2D g_GBufferMotion : register(t3);
Texture2D g_DepthBuffer : register(t4);
RWStructuredBuffer<RTXDI_PackedDIReservoir> g_LightReservoirs : register(u0);
RWTexture2D<float4> g_ReSTIRShadingOutput : register(u1);

[numthreads(8, 8, 1)]
void CS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    // assign global resources in RAB
    g_RAB_SceneTLAS = g_SceneTLAS;
    g_RAB_LightInfoBuffer = g_LightInfoBuffer;
    g_RAB_LightReservoirs = g_LightReservoirs;

    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[dispatchThreadID.xy], g_GBufferMotion[dispatchThreadID.xy], gbufferParams);

    float depthBufferValue = g_DepthBuffer[dispatchThreadID.xy].x;
    float3 worldPosition = ScreenUVToWorldPosition(dispatchThreadID.xy, depthBufferValue, g_ReSTIRLightingConstants.m_ClipToWorld);

    RAB_Surface surface;
    surface.m_WorldPos = worldPosition;
    surface.m_ViewDir = normalize(worldPosition - g_ReSTIRLightingConstants.m_CameraPosition);
    surface.m_ViewDepth = (depthBufferValue == 0.0f) ? length(worldPosition - g_ReSTIRLightingConstants.m_CameraPosition) : kKindaBigNumber;
    surface.m_Normal = gbufferParams.m_Normal;
    surface.m_GeometryNormal = gbufferParams.m_GeometryNormal;
    surface.m_DiffuseAlbedo = gbufferParams.m_Albedo.rgb;
    surface.m_SpecularF0 = ComputeF0(gbufferParams.m_Albedo.rgb, gbufferParams.m_Metallic);
    surface.m_Roughness = gbufferParams.m_Roughness;
    surface.m_DiffuseProbability = GetSurfaceDiffuseProbability(surface);

    float4 ReSTIRShadingOutput = float4(0, 0, 0, 1);
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(surface))
    {
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(dispatchThreadID.xy, 1);
        RAB_RandomSamplerState coherentRng = RAB_InitRandomSampler(dispatchThreadID.xy / RTXDI_TILE_SIZE_IN_PIXELS, 1);
        
        const uint kNumLocalLightSamples = 0;
        const uint kInfiniteLightSamples = 1;
        const uint kEnvMapSamples = 0;
        const uint kNumInitialBRDFSamples = 1;
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(kNumLocalLightSamples, kInfiniteLightSamples, kEnvMapSamples, kNumInitialBRDFSamples);
        
        const ReSTIRDI_LocalLightSamplingMode kLocalLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_UNIFORM;

        RAB_LightSample lightSample;
        RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, coherentRng, surface, sampleParams, g_ReSTIRLightingConstants.m_LightBufferParams, kLocalLightSamplingMode, lightSample);

        const bool kEnableInitialVisibility = true;
        if (kEnableInitialVisibility && RTXDI_IsValidDIReservoir(reservoir))
        {
            if (RAB_GetConservativeVisibility(surface, lightSample))
            {
                // TODO: only dir light for now
                float3 dirLightShading = EvaluateDirectionalLight(gbufferParams, g_ReSTIRLightingConstants.m_CameraPosition, worldPosition, lightSample.m_Normal, lightSample.m_Radiance.x);
                ReSTIRShadingOutput.rgb = dirLightShading * RTXDI_GetDIReservoirInvPdf(reservoir);
            }
            else
            {
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }
    }

    g_ReSTIRShadingOutput[dispatchThreadID.xy] = ReSTIRShadingOutput;
    RTXDI_StoreDIReservoir(reservoir, g_ReSTIRLightingConstants.m_ReservoirBufferParams, dispatchThreadID.xy, g_ReSTIRLightingConstants.m_OutputBufferIndex);
}
