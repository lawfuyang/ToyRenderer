#include "toyrenderer_common.hlsli"
#include "ShaderInterop.h"

cbuffer GIProbeVisualizationConstsBuffer : register(b0) { GIProbeVisualizationConsts g_GIProbeVisualizationConsts; }

void VS_Main
(
    in float3 inPosition : POSITION,
    in float3 inNormal : NORMAL,
    in float2 inTexCoord : TEXCOORD,
    out float4 outPosition : SV_POSITION,
    out float3 outNormal : TEXCOORD0,
    out float2 outTexCoord : TEXCOORD1
)
{
    outPosition = float4(inPosition, 1.0f);
    outNormal = inNormal;
    outTexCoord = inTexCoord;
}

void PS_Main
(
    in float4 inPosition : SV_POSITION,
    in float3 inNormal : TEXCOORD0,
    in float2 inTexCoord : TEXCOORD1,
    out float4 outColor : SV_Target
)
{
    outColor = 0;
}
