// NOTE: this whole file is essentially a duplicate of DDGIExtraReductionCS, but with divide-by-zero fix

#include "toyrenderer_common.hlsli"

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"

#include "ShaderInterop.h"

//GIProbeExtraReductionConsts
cbuffer GIProbeExtraReductionConstsBuffer : register(b0) { GIProbeExtraReductionConsts g_GIProbeExtraReductionConsts; }
StructuredBuffer<DDGIVolumeDescGPUPacked> g_DDGIVolumes : register(t0);
RWTexture2DArray<float4> g_ProbeVariabilityAverage : register(u0);

#define NUM_THREADS_X 4
#define NUM_THREADS_Y 8
#define NUM_THREADS_Z 4
#define NUM_THREADS NUM_THREADS_X*NUM_THREADS_Y*NUM_THREADS_Z
#define NUM_WAVES NUM_THREADS / RTXGI_DDGI_WAVE_LANE_COUNT

groupshared uint MaxSumEntry;
groupshared float ThreadGroupAverage[NUM_WAVES];
groupshared uint MaxAverageEntry;
groupshared float ThreadGroupWeight[NUM_WAVES];

void reduceSharedMemoryAverage(uint ThreadIndexInGroup, uint waveIndex, uint waveLaneCount)
{
    uint numSharedMemoryEntries = MaxAverageEntry + 1;
    uint activeThreads = numSharedMemoryEntries;
    while (activeThreads > 1)
    {
        bool usefulThread = ThreadIndexInGroup < activeThreads;
        if (usefulThread)
        {
            float value = ThreadGroupAverage[ThreadIndexInGroup];
            float weight = ThreadGroupWeight[ThreadIndexInGroup];
            GroupMemoryBarrierWithGroupSync();

            float waveTotalValue = WaveActiveSum(weight * value);
            float waveTotalWeight = WaveActiveSum(weight);
            float TotalPossibleWeight = WaveActiveCountBits(true);

            if (WaveIsFirstLane())
            {
                ThreadGroupAverage[waveIndex] = waveTotalValue / waveTotalWeight;
                ThreadGroupWeight[waveIndex] = waveTotalWeight / TotalPossibleWeight;
            }
            GroupMemoryBarrierWithGroupSync();
        }
        activeThreads = (activeThreads + waveLaneCount - 1) / waveLaneCount;
    }
}

[numthreads(NUM_THREADS_X, NUM_THREADS_Y, NUM_THREADS_Z)]
void CS_DDGIExtraReduction(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID, uint ThreadIndexInGroup : SV_GroupIndex)
{
    if (ThreadIndexInGroup == 0)
    {
        MaxAverageEntry = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint volumeIndex = 0;
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_DDGIVolumes[volumeIndex]);
    
    uint waveLaneCount = WaveGetLaneCount();
    uint wavesPerThreadGroup = NUM_THREADS / waveLaneCount;
    uint waveIndex = ThreadIndexInGroup / waveLaneCount;

    // Doing 4x2 samples per thread
    const uint3 ThreadSampleFootprint = uint3(4, 2, 1);

    uint3 groupCoordOffset = GroupID.xyz * uint3(NUM_THREADS_X, NUM_THREADS_Y, NUM_THREADS_Z) * ThreadSampleFootprint;
    uint3 threadCoordInGroup = GroupThreadID.xyz;
    uint3 threadCoordGlobal = groupCoordOffset + threadCoordInGroup * ThreadSampleFootprint;
    uint3 inputSize = g_GIProbeExtraReductionConsts.m_ReductionInputSize;

    bool footprintInBounds = all(threadCoordGlobal < inputSize);
    float threadFootprintValueSum = 0;
    float threadFootprintWeightSum = 0;

    if (footprintInBounds)
    {
        for (uint i = 0; i < ThreadSampleFootprint.x; i++)
        {
            for (uint j = 0; j < ThreadSampleFootprint.y; j++)
            {
                uint3 sampleCoord = threadCoordGlobal + uint3(i, j, 0);
                bool sampleInBounds = all(sampleCoord < inputSize);
                if (sampleInBounds)
                {
                    float value = g_ProbeVariabilityAverage[sampleCoord].r;
                    float weight = g_ProbeVariabilityAverage[sampleCoord].g;
                    threadFootprintValueSum += weight * value;
                    threadFootprintWeightSum += weight;
                }
            }
        }
    }
    float threadAverageValue = (footprintInBounds && threadFootprintWeightSum > 0) ? threadFootprintValueSum / threadFootprintWeightSum : 0;
    // Per-thread weight will be 1.0 if thread sampled all 4x2 pixels, 0.125 if it only sampled one
    float ThreadTotalPossibleWeight = ThreadSampleFootprint.x * ThreadSampleFootprint.y;
    float threadWeight = threadFootprintWeightSum / ThreadTotalPossibleWeight;

    // Sum up the warp
    float waveTotalValue = WaveActiveSum(threadWeight * threadAverageValue);
    float waveTotalWeight = WaveActiveSum(threadWeight);
    float waveTotalPossibleWeight = waveLaneCount * ThreadTotalPossibleWeight;

    if (WaveIsFirstLane() && WaveActiveAnyTrue(footprintInBounds))
    {
        ThreadGroupAverage[waveIndex] = (waveTotalWeight > 0) ? (waveTotalValue / waveTotalWeight) : 0;
        ThreadGroupWeight[waveIndex] = waveTotalWeight / waveTotalPossibleWeight;
        InterlockedMax(MaxSumEntry, waveIndex);
    }

    GroupMemoryBarrierWithGroupSync();
    reduceSharedMemoryAverage(ThreadIndexInGroup, waveIndex, waveLaneCount);
    if (ThreadIndexInGroup == 0)
    {
        g_ProbeVariabilityAverage[GroupID.xyz].r = ThreadGroupAverage[0];
        g_ProbeVariabilityAverage[GroupID.xyz].g = ThreadGroupWeight[0];
    }
}