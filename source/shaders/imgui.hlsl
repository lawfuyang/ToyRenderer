struct IMGUIConstants
{
    float4x4 m_ProjMatrix;
};
cbuffer g_ConstantBuffer : register(b0) { IMGUIConstants g_IMGUIConstants; }
sampler g_LinearWrapSampler : register(s0);
Texture2D g_IMGUIFontTexture : register(t0);

void VS_Main(
    in float2 inPosition : POSITION,
    in float2 inUV : TEXCOORD0,
    in float4 inColor : COLOR0,
    out float4 outPosition : SV_POSITION,
    out float4 outColor : COLOR0,
    out float2 outUV : TEXCOORD0)
{
    outPosition = mul(float4(inPosition, 0.0f, 1.0f), g_IMGUIConstants.m_ProjMatrix);
    outColor = inColor;
    outUV = inUV;
}

void PS_Main(
    in float4 inPosition : SV_POSITION,
    in float4 inColor : COLOR0,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_Target)
{
    outColor = inColor * g_IMGUIFontTexture.Sample(g_LinearWrapSampler, inUV);
}
