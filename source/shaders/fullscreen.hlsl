#include "common.hlsli"
	
void VS_FullScreenTriangle(
    in uint inVertexID : SV_VertexID,
    out float4 outPosition : SV_POSITION,
    out float2 outUV : TEXCOORD0
)
{
    float4 positions[3] =
    {
        { -1.0f, 3.0f, kFarDepth, 1.0f },
        { -1.0f, -1.0f, kFarDepth, 1.0f },
        { 3.0f, -1.0f, kFarDepth, 1.0f }
    };
    
    float2 UVs[3] = 
    {
        { 0.0f, -1.0f },
        { 0.0f, 1.0f },
        { 2.0f, 1.0f }
    };
    
    outPosition = positions[inVertexID];
    outUV = UVs[inVertexID];
}

void VS_FullScreenCube(
    in uint inVertexID : SV_VertexID,
    out float4 outPosition : SV_POSITION
)
{
    float4 vertex[] =
    {
        { -1.0f, -1.0f, kFarDepth,        1 },
        { -1.0f, -1.0f, 1.0f - kFarDepth, 1 },
        {  1.0f, -1.0f, 1.0f - kFarDepth, 1 },
        {  1.0f, -1.0f, kFarDepth,        1 },
        { -1.0f,  1.0f, kFarDepth,        1 },
        {  1.0f,  1.0f, kFarDepth,        1 },
        {  1.0f,  1.0f, 1.0f - kFarDepth, 1 },
        { -1.0f,  1.0f, 1.0f - kFarDepth, 1 }
    };

    int index[] =
    {
        1, 2, 3,
        3, 4, 1,
        5, 6, 7,
        7, 8, 5,
        1, 4, 6,
        6, 5, 1,
        4, 3, 7,
        7, 6, 4,
        3, 2, 8,
        8, 7, 3,
        2, 1, 5,
        5, 8, 2
    };
	
    outPosition = vertex[index[inVertexID] - 1];
}

Texture2D g_Input : register(t0);

void PS_Passthrough(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target
)
{
    // TODO: add support for different formats?
    // TODO: add support for different samplers?
    // TODO: add support for different input & output resolutions?
    outColor = g_Input[inPosition.xy];
}
