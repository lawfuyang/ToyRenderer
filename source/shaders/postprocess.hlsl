#include "toyrenderer_common.hlsli"

#include "ShaderInterop.h"

cbuffer PostProcessParametersConstantBuffer : register(b0){ PostProcessParameters g_PostProcessParameters; }
Texture2D g_ColorInput : register(t0);
StructuredBuffer<float> g_AverageLuminanceBuffer : register(t1);
Texture2D g_BloomTexture : register(t2);

float3 ACES_Fast(float3 x)
{
	// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.
float3 PBRNeutralToneMapping(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return lerp(color, newPeak * float3(1, 1, 1), g);
}

void PS_PostProcess(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_TARGET)
{
    float3 rgb = g_ColorInput[inPosition.xy].rgb;
    
    // bloom
    float3 bloomSrc = g_BloomTexture[inPosition.xy].rgb;
    rgb = lerp(rgb, bloomSrc, g_PostProcessParameters.m_BloomStrength);
    
    float sceneLuminance = g_PostProcessParameters.m_ManualExposure;
    if (sceneLuminance == 0.0f)
    {
        sceneLuminance = g_AverageLuminanceBuffer[0];
    }
    float fLumScale = g_PostProcessParameters.m_MiddleGray / sceneLuminance;
    rgb *= fLumScale;
    
    //rgb = ACES_Fast(rgb);
    rgb = PBRNeutralToneMapping(rgb);
    
    rgb = LinearToSRGB(rgb);

    outColor = float4(rgb, 1.0f);
}
