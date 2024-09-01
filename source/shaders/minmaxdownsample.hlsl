#include "common.h"

#include "shared/MinMaxDownsampleStructs.h"

cbuffer g_MinMaxDownsampleConstsBuffer : register(b0) { MinMaxDownsampleConsts g_MinMaxDownsampleConsts; }
Texture2D<float> g_Input : register(t0);
RWTexture2D<float> g_Output : register(u0);
SamplerState g_PointClampSampler : register(s0);

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
    
    float2 uv = (dispatchThreadID.xy + 0.5f) / g_MinMaxDownsampleConsts.m_OutputDimensions;
    float4 depths = g_Input.Gather(g_PointClampSampler, uv);
    
    float output;
    if (g_MinMaxDownsampleConsts.m_bDownsampleMax)
    {
        output = Max4(depths.x, depths.y, depths.z, depths.w);
    }
    else
    {
        output = Min4(depths.x, depths.y, depths.z, depths.w);
    }
    
    g_Output[dispatchThreadID.xy] = output;
}
