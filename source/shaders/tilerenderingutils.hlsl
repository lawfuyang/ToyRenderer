#include "common.h"

#include "shared/IndirectArguments.h"
#include "shared/TileRenderingStructs.h"

RWStructuredBuffer<DispatchIndirectArguments> g_DispatchIndirectArgsClearOutput : register(u0);
RWStructuredBuffer<DrawIndirectArguments> g_DrawIndirectArgsClearOutput : register(u1);

[numthreads(8, 1, 1)]
void CS_ClearIndirectParams(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    DispatchIndirectArguments dispatchIndirectArgs;
    dispatchIndirectArgs.m_ThreadGroupCountX = 0;
    dispatchIndirectArgs.m_ThreadGroupCountY = 1;
    dispatchIndirectArgs.m_ThreadGroupCountZ = 1;
    
    DrawIndirectArguments drawIndirectArgs;
    drawIndirectArgs.m_VertexCount = 6; // 2 triangles
    drawIndirectArgs.m_InstanceCount = 0;
    drawIndirectArgs.m_StartVertexLocation = 0;
    drawIndirectArgs.m_StartInstanceLocation = 0;
    
    g_DispatchIndirectArgsClearOutput[dispatchThreadID.x] = dispatchIndirectArgs;
    g_DrawIndirectArgsClearOutput[dispatchThreadID.x] = drawIndirectArgs;
}

cbuffer g_TileRenderingConstsBuffer : register(b99) { TileRenderingConsts g_TileRenderingConsts; }
StructuredBuffer<uint2> g_TileOffsets : register(t99);

void VS_Main(
    in uint inVertexID : SV_VertexID,
    in uint inInstanceID : SV_InstanceID,
    out float4 outPosition : SV_POSITION,
    out float2 outUV : TEXCOORD0
)
{
    const float2 kQuadUVs[6] =
    {
        { 0.0f, 0.0f }, // top left
        { 0.0f, 1.0f }, // bottom left
        { 1.0f, 1.0f }, // bottom right
        { 1.0f, 1.0f }, // bottom right
        { 1.0f, 0.0f }, // top right
        { 0.0f, 0.0f }  // top left
    };
    
    // we compute quad positions in UV space first, and convert to clip space at the end
    // pretty sure there's a more optimized solution, but its simpler this way, i'm lazy & i don't care.
    // the performance difference should be negligible anyway
    
    // get base normalized positions for top left quad
    float2 screenUV = kQuadUVs[inVertexID] * g_TileRenderingConsts.m_TileSize / g_TileRenderingConsts.m_OutputDimensions;
    
    // offset to tile position. tile offsets are in texels, so we divide by the output dimensions to get the normalized offset
    screenUV += (float2)g_TileOffsets[g_TileRenderingConsts.m_NbTiles * g_TileRenderingConsts.m_TileID + inInstanceID] / g_TileRenderingConsts.m_OutputDimensions;

    outPosition = float4(UVToClipXY(screenUV), kFarDepth, 1.0f);
    outUV = screenUV;
}
