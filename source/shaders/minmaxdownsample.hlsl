#include "toyrenderer_common.hlsli"

#include "ShaderInterop.h"

cbuffer g_MinMaxDownsampleConstsBuffer : register(b0) { MinMaxDownsampleConsts g_MinMaxDownsampleConsts; }

[numthreads(8, 8, 1)]
void CS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    if (any(dispatchThreadID.xy >= g_MinMaxDownsampleConsts.m_OutputDimensions))
    {
        return;
    }
    
    Texture2D<float> inputTexture = ResourceDescriptorHeap[g_MinMaxDownsampleConsts.m_InputIdx];
    RWTexture2D<float> outputTexture = ResourceDescriptorHeap[g_MinMaxDownsampleConsts.m_OutputIdxInHeap];
    SamplerState pointClampSampler = SamplerDescriptorHeap[g_MinMaxDownsampleConsts.m_PointClampSamplerIdx];
    
    float2 uv = (dispatchThreadID.xy + 0.5f) / g_MinMaxDownsampleConsts.m_OutputDimensions;
    float4 depths = inputTexture.Gather(pointClampSampler, uv);
    
    float output;
    if (g_MinMaxDownsampleConsts.m_bDownsampleMax)
    {
        output = Max4(depths.x, depths.y, depths.z, depths.w);
    }
    else
    {
        output = Min4(depths.x, depths.y, depths.z, depths.w);
    }
    
    outputTexture[dispatchThreadID.xy] = output;
}
