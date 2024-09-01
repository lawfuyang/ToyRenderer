#include "common.h"

#include "shared/BloomStructs.h"

cbuffer g_BloomDownsampleConstsBuffer : register(b0) { BloomDownsampleConsts g_BloomDownsampleConsts; }
Texture2D g_DownsampleSourceTexture : register(t0);
sampler g_LinearClampSampler : register(s0);

float KarisAverage(float3 col)
{
	// Formula is 1 / (1 + luma)
    float luma = RGBToLuminance(col) * 0.25f;
    return 1.0f / (1.0f + luma);
}

void PS_Downsample(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    float x = g_BloomDownsampleConsts.m_InvSourceResolution.x;
    float y = g_BloomDownsampleConsts.m_InvSourceResolution.y;
    
    // Take 13 samples around current texel:
	// a - b - c
	// - j - k -
	// d - e - f
	// - l - m -
	// g - h - i
	// === ('e' is the current texel) ===    
    float3 a = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - 2 * x, inUV.y + 2 * y), 0).rgb;
    float3 b = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y + 2 * y), 0).rgb;
    float3 c = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + 2 * x, inUV.y + 2 * y), 0).rgb;

    float3 d = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - 2 * x, inUV.y), 0).rgb;
    float3 e = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y), 0).rgb;
    float3 f = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + 2 * x, inUV.y), 0).rgb;

    float3 g = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - 2 * x, inUV.y - 2 * y), 0).rgb;
    float3 h = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y - 2 * y), 0).rgb;
    float3 i = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + 2 * x, inUV.y - 2 * y), 0).rgb;

    float3 j = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - x, inUV.y + y), 0).rgb;
    float3 k = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + x, inUV.y + y), 0).rgb;
    float3 l = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - x, inUV.y - y), 0).rgb;
    float3 m = g_DownsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + x, inUV.y - y), 0).rgb;

	// Apply weighted distribution:
	// 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
	// a,b,d,e * 0.125
	// b,c,e,f * 0.125
	// d,e,g,h * 0.125
	// e,f,h,i * 0.125
	// j,k,l,m * 0.5
	// This shows 5 square areas that are being sampled. But some of them overlap, so to have an energy preserving downsample we need to make some adjustments.
	// The weights are the distributed, so that the sum of j,k,l,m (e.g.) contribute 0.5 to the final color output.
    // The code below is written to effectively yield this sum. We get: 0.125*5 + 0.03125*4 + 0.0625*4 = 1
    
    float3 downsample = 0;
    
    // Check if we need to perform Karis average on each block of 4 samples
    if (g_BloomDownsampleConsts.m_bIsFirstDownsample)
    {
        float3 groups[5];
        
        // We are writing to mip 0, so we need to apply Karis average to each block of 4 samples to prevent fireflies (very bright subpixels, leads to pulsating artifacts)
        groups[0] = (a + b + d + e) * (0.125f / 4.0f);
        groups[1] = (b + c + e + f) * (0.125f / 4.0f);
        groups[2] = (d + e + g + h) * (0.125f / 4.0f);
        groups[3] = (e + f + h + i) * (0.125f / 4.0f);
        groups[4] = (j + k + l + m) * (0.5f / 4.0f);
        groups[0] *= KarisAverage(groups[0]);
        groups[1] *= KarisAverage(groups[1]);
        groups[2] *= KarisAverage(groups[2]);
        groups[3] *= KarisAverage(groups[3]);
        groups[4] *= KarisAverage(groups[4]);
        downsample = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];
        downsample = max(downsample, 0.0001f);
    }
    else
    {
        downsample = e * 0.125f; // ok
        downsample += (a + c + g + i) * 0.03125f; // ok
        downsample += (b + d + f + h) * 0.0625f; // ok
        downsample += (j + k + l + m) * 0.125f; // ok
    }
    
    outColor = float4(downsample, 1.0f);
}

cbuffer g_BloomUpsampleConstsBuffer : register(b0) { BloomUpsampleConsts g_BloomUpsampleConsts; }
Texture2D g_UpsampleSourceTexture : register(t0);

void PS_Upsample(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    // The filter kernel is applied with a radius, specified in texture coordinates, so that the radius will vary across mip resolutions.
    float x = g_BloomUpsampleConsts.m_FilterRadius;
    float y = g_BloomUpsampleConsts.m_FilterRadius;

	// Take 9 samples around current texel:
	// a - b - c
	// d - e - f
	// g - h - i
	// === ('e' is the current texel) ===
    float3 a = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - x, inUV.y + y), 0).rgb;
    float3 b = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y + y), 0).rgb;
    float3 c = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + x, inUV.y + y), 0).rgb;

    float3 d = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - x, inUV.y), 0).rgb;
    float3 e = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y), 0).rgb;
    float3 f = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + x, inUV.y), 0).rgb;

    float3 g = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x - x, inUV.y - y), 0).rgb;
    float3 h = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x, inUV.y - y), 0).rgb;
    float3 i = g_UpsampleSourceTexture.SampleLevel(g_LinearClampSampler, float2(inUV.x + x, inUV.y - y), 0).rgb;

	// Apply weighted distribution, by using a 3x3 tent filter:
	//  1   | 1 2 1 |
	// -- * | 2 4 2 |
	// 16   | 1 2 1 |
    float3 upsample = e * 4.0f;
    upsample += (b + d + f + h) * 2.0f;
    upsample += (a + c + g + i);
    upsample *= 1.0f / 16.0f;
    
    outColor = float4(upsample, 1.0f);
}
