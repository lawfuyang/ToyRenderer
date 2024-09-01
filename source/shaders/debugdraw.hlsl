Texture2D g_GlyphsTexture : register(t0);
SamplerState g_LinearClampSampler : register(s0);

cbuffer ConstantBufferData : register(b0)
{
    matrix viewProjectionMatrix;
    float4 screenDimensions;
};

void VS_LinePoint(
    in float3 inPos : POSITION,
    in float3 inUV : TEXCOORD,
    in float3 inColor : COLOR,
    out float4 outPos : SV_POSITION,
    out float2 outUV : TEXCOORD,
    out float3 outColor : COLOR
)
{
    outPos = mul(float4(inPos, 1.0f), viewProjectionMatrix);
    outUV = inUV.xy;
    outColor = inColor;
}

void PS_LinePoint(
    in float4 inPos : SV_POSITION,
    in float2 inUV : TEXCOORD,
    in float3 inColor : COLOR,
    out float4 outColor : SV_TARGET
)
{
    outColor = float4(inColor, 1.0f);
}

void VS_TextGlyph(
    in float3 inPos : POSITION,
    in float3 inUV : TEXCOORD,
    in float3 inColor : COLOR,
    out float4 outPos : SV_POSITION,
    out float2 outUV : TEXCOORD,
    out float3 outColor : COLOR
)
{
    // Map to normalized clip coordinates:
    float x = ((2.0 * (inPos.x - 0.5)) / screenDimensions.x) - 1.0;
    float y = 1.0 - ((2.0 * (inPos.y - 0.5)) / screenDimensions.y);
    
    outPos = float4(x, y, 0.0, 1.0);
    outUV = inUV.xy;
    outColor = inColor;
}

void PS_TextGlyph(
    in float4 inPos : SV_POSITION,
    in float2 inUV : TEXCOORD,
    in float3 inColor : COLOR,
    out float4 outColor : SV_TARGET
)
{
    float4 texColor = g_GlyphsTexture.Sample(g_LinearClampSampler, inUV);
    float4 fragColor = float4(inColor, 1.0f);

    fragColor.a = texColor.r;
    outColor = fragColor;
}
