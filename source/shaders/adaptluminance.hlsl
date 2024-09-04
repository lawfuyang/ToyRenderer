#include "common.hlsli"

#include "shared/AdaptLuminanceStructs.h"

cbuffer GenerateLuminanceHistogramParametersConstantBuffer : register(b0)
{
    GenerateLuminanceHistogramParameters g_GenerateLuminanceHistogramParameters;
}
Texture2D g_SrcColor : register(t0);
RWStructuredBuffer<uint> g_HistogramOut : register(u0);

// Shared histogram buffer used for storing intermediate sums for each work group
groupshared uint gs_Histogram[256];

[numthreads(16, 16, 1)]
void CS_GenerateLuminanceHistogram(
    uint groupID : SV_GroupIndex,
    uint3 threadID : SV_DispatchThreadID)
{
    // Initialize the bin for this thread to 0
    gs_Histogram[groupID] = 0;
    GroupMemoryBarrierWithGroupSync();

    if (all(threadID.xy < g_GenerateLuminanceHistogramParameters.m_SrcColorDims))
    {
        float3 hdrColor = g_SrcColor.Load(int3(threadID.xy, 0)).rgb;

        // Convert our RGB value to Luminance
        float lum = RGBToLuminance(hdrColor);

        // Avoid taking the log of zero
        uint binIndex = 0;
        if (lum >= 0.005f)
        {
            // Calculate the log_2 luminance and express it as a value in [0.0, 1.0] where 0.0 represents the minimum luminance, and 1.0 represents the max.
            float logLum = saturate((log2(lum) - g_GenerateLuminanceHistogramParameters.m_MinLogLuminance) * g_GenerateLuminanceHistogramParameters.m_InverseLogLuminanceRange);

            // For a given color and luminance range, get the histogram bin index. Map [0, 1] to [1, 255]. The zeroth bin is handled by the epsilon check above.
            binIndex = uint(logLum * 254.0 + 1.0);
        }

        // We use an atomic add to ensure we don't write to the same bin in our histogram from two different threads at the same time.
        InterlockedAdd(gs_Histogram[binIndex], 1);
    }

    // Wait for all threads in the work group to reach this point before adding our local histogram to the global one
    GroupMemoryBarrierWithGroupSync();

    // Technically there's no chance that two threads write to the same bin here, but different work groups might! So we still need the atomic add.
    uint dummy = 0;
    InterlockedAdd(g_HistogramOut[groupID], gs_Histogram[groupID], dummy);
}

cbuffer AdaptExposureParametersConstantBuffer : register(b0)
{
    AdaptExposureParameters g_AdaptExposureParameters;
}
StructuredBuffer<uint> g_Histogram : register(t0);
RWStructuredBuffer<float> g_LuminanceBuffer : register(u0);

groupshared uint gs_LuminanceBins[256];

[numthreads(256, 1, 1)]
void CS_AdaptExposure(
    uint groupID : SV_GroupIndex,
    uint3 threadID : SV_DispatchThreadID)
{
    // Get the count from the histogram buffer
    uint countForThisBin = g_Histogram[threadID.x];
    gs_LuminanceBins[threadID.x] = countForThisBin * threadID.x;
    GroupMemoryBarrierWithGroupSync();

    // This loop will perform a weighted count of the luminance range
    [unroll]
    for (uint binIndex = (256 >> 1); binIndex > 0; binIndex >>= 1)
    {
        if (threadID.x < binIndex)
        {
            gs_LuminanceBins[threadID.x] += gs_LuminanceBins[threadID.x + binIndex];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // We only need to calculate this once, so only a single thread is needed.
    if (threadID.x == 0)
    {
        // Here we take our weighted sum and divide it by the number of pixels that had luminance greater than zero.
        // Since the index == 0, we can use countForThisBin to find the number of black pixels
        float weightedLogAverage = (gs_LuminanceBins[0] / max(g_AdaptExposureParameters.m_NbPixels - float(countForThisBin), 1.0)) - 1.0;

        // Map from our histogram space to actual luminance
        float weightedAvgLum = exp2(weightedLogAverage / 254.0 * g_AdaptExposureParameters.m_LogLuminanceRange + g_AdaptExposureParameters.m_MinLogLuminance);

        // The new stored value will be interpolated using the last frames value to prevent sudden shifts in the exposure.
        float lumLastFrame = g_LuminanceBuffer[0];
        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * g_AdaptExposureParameters.m_AdaptationSpeed;
        g_LuminanceBuffer[0] = adaptedLum;
    }
}
