#include "toyrenderer_common.hlsli"
#include "ShaderInterop.h"

Texture2D g_Input : register(t0);
sampler g_LinearClampSampler : register(s0);

void PS_VisualizeMinMip(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target
)
{
    static const float3 kMipColors[8] =
    {
        { 1.0f, 0.0f, 0.0f }, // Mip 0 - Red
        { 1.0f, 0.5f, 0.0f }, // Mip 1 - Orange
        { 1.0f, 1.0f, 0.0f }, // Mip 2 - Yellow
        { 0.5f, 1.0f, 0.0f }, // Mip 3 - Light Green
        { 0.0f, 1.0f, 0.0f }, // Mip 4 - Green
        { 0.0f, 0.5f, 1.0f }, // Mip 5 - Light Blue
        { 0.0f, 0.0f, 1.0f }, // Mip 6 - Blue
        { 0.5f, 0.0f, 1.0f }  // Mip 7 - Purple
    };

    float minMipData = g_Input.Sample(g_LinearClampSampler, inUV).r;
    uint mip = min(7, (uint)(minMipData));

    outColor = float4(kMipColors[mip], 1.0f);
}
