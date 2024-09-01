#include "Common.h"

#include "shared/PostProcessStructs.h"

cbuffer PostProcessParametersConstantBuffer : register(b0){ PostProcessParameters g_PostProcessParameters; }
Texture2D g_ColorInput : register(t0);
StructuredBuffer<float> g_AverageLuminanceBuffer : register(t1);
Texture2D g_BloomTexture : register(t2);
sampler g_LinearClampSampler : register(s0);

float3 FXAA(uint2 vpos, float2 texCoord)
{
    const float FXAA_SPAN_MAX = 8.0;
    const float FXAA_REDUCE_MUL = 1.0 / 8.0;
    const float FXAA_REDUCE_MIN = 1.0 / 128.0;

    float3 rgbNW = g_ColorInput.Load(int3(vpos, 0), int2(-1.0, -1.0)).rgb;
    float3 rgbNE = g_ColorInput.Load(int3(vpos, 0), int2(1.0, -1.0)).rgb;
    float3 rgbSW = g_ColorInput.Load(int3(vpos, 0), int2(-1.0, 1.0)).rgb;
    float3 rgbSE = g_ColorInput.Load(int3(vpos, 0), int2(1.0, 1.0)).rgb;
    float3 rgbM = g_ColorInput.Load(int3(vpos, 0)).rgb;

    float lumaNW = RGBToLuminance(rgbNW);
    float lumaNE = RGBToLuminance(rgbNE);
    float lumaSW = RGBToLuminance(rgbSW);
    float lumaSE = RGBToLuminance(rgbSE);
    float lumaM = RGBToLuminance(rgbM);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL),
        FXAA_REDUCE_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(float2(FXAA_SPAN_MAX, FXAA_SPAN_MAX),
        max(float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
            dir * rcpDirMin)) / g_PostProcessParameters.m_OutputDims;

    float3 rgbA = (1.0 / 2.0) *
        (
            g_ColorInput.Sample(g_LinearClampSampler, texCoord + dir * (1.0 / 3.0 - 0.5), 0).rgb +
            g_ColorInput.Sample(g_LinearClampSampler, texCoord + dir * (2.0 / 3.0 - 0.5), 0).rgb
            );

    float3 rgbB = rgbA * (1.0 / 2.0) + (1.0 / 4.0) *
        (
            g_ColorInput.Sample(g_LinearClampSampler, texCoord + dir * (0.0 / 3.0 - 0.5), 0).rgb +
            g_ColorInput.Sample(g_LinearClampSampler, texCoord + dir * (3.0 / 3.0 - 0.5), 0).rgb
            );

    float lumaB = RGBToLuminance(rgbB);

    float3 finalColor;
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        finalColor = rgbA;
    }
    else
    {
        finalColor = rgbB;
    }
    return finalColor;
}

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

void PS_PostProcess(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_TARGET)
{
    // FXAA
    //float3 HDRColor = g_ColorInput[inPosition.xy].rgb;
    float3 rgb = FXAA(inPosition.xy, inUV);
    
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
    
    rgb = ACES_Fast(rgb);
    
    rgb = LinearToSRGB(rgb);

    outColor = float4(rgb, 1.0f);
}
