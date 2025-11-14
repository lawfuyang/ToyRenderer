#include "toyrenderer_common.hlsli"

#include "RtxdiApplicationBridge.hlsli"

#include "raytracingcommon.hlsli"

#include "shaderinterop.h"

struct ReSTIRLightingConstants
{
    RTXDI_LightBufferParameters m_LightBufferParams;
    RTXDI_ReservoirBufferParameters m_ReservoirBufferParams;
    Matrix m_ClipToWorld;
    Vector3 m_CameraPosition;
    uint32_t pad0;
    Vector2 m_OutputResolutionInv;
};

cbuffer g_ReSTIRLightingConstantsBuffer : register(b0) { ReSTIRLightingConstants g_ReSTIRLightingConstants; }
RaytracingAccelerationStructure g_SceneTLAS : register(t0);
StructuredBuffer<RAB_LightInfo> g_LightInfoBuffer : register(t1);
RWStructuredBuffer<RTXDI_PackedDIReservoir> g_LightReservoirs : register(u0);

struct PrimarySurfaceOutput
{
    RAB_Surface m_Surface;
    float3 m_MotionVector;
    float3 m_EmissiveColor;
};

PrimarySurfaceOutput TracePrimaryRay(int2 pixelPosition)
{
    RayDesc rayDesc = SetupScreenspaceRay(
        pixelPosition,
        g_ReSTIRLightingConstants.m_OutputResolutionInv,
        g_ReSTIRLightingConstants.m_ClipToWorld,
        g_ReSTIRLightingConstants.m_CameraPosition);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
    rayQuery.TraceRayInline(g_SceneTLAS, RAY_FLAG_NONE, 0xFF, rayDesc);
    rayQuery.Proceed();

    PrimarySurfaceOutput result;
    result.m_Surface = RAB_EmptySurface();
    result.m_MotionVector = 0;
    result.m_EmissiveColor = 0;

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {

    }

    return result;
}

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

    PrimarySurfaceOutput primary = TracePrimaryRay(dispatchThreadID.xy);
}
