#include "common.hlsli"

#include "shared/SkyStructs.h"

cbuffer SkyPassParametersCBuffer : register(b0)
{
    SkyPassParameters g_SkyPassParameters;
}

float3 HosekWilkie(float cosTheta, float gamma, float cosGamma)
{
    float3 A = g_SkyPassParameters.m_HosekParams.m_Params[0].xyz;
    float3 B = g_SkyPassParameters.m_HosekParams.m_Params[1].xyz;
    float3 C = g_SkyPassParameters.m_HosekParams.m_Params[2].xyz;
    float3 D = g_SkyPassParameters.m_HosekParams.m_Params[3].xyz;
    float3 E = g_SkyPassParameters.m_HosekParams.m_Params[4].xyz;
    float3 F = g_SkyPassParameters.m_HosekParams.m_Params[5].xyz;
    float3 G = g_SkyPassParameters.m_HosekParams.m_Params[6].xyz;
    float3 H = g_SkyPassParameters.m_HosekParams.m_Params[7].xyz;
    float3 I = g_SkyPassParameters.m_HosekParams.m_Params[8].xyz;
    
    float3 chi = (1 + cosGamma * cosGamma) / pow(1 + H * H - 2 * cosGamma * H, float3(1.5f, 1.5f, 1.5f));
    return (1 + A * exp(B / (cosTheta + 0.01))) * (C + D * exp(E * gamma) + F * (cosGamma * cosGamma) + G * chi + I * sqrt(cosTheta));
}

void PS_HosekWilkieSky(
    in float4 inPosition : SV_POSITION,
    in float2 inUV : TEXCOORD0,
    out float4 outColor : SV_TARGET
)
{
    float3 worldPosition = ScreenUVToWorldPosition(inUV, 0.9f, g_SkyPassParameters.m_ClipToWorld); // NOTE: depth input doesnt matter. we just want a world position so we can get 'V' vector
    float3 V = normalize(worldPosition - g_SkyPassParameters.m_CameraPosition);
    
    float cos_theta = clamp(V.y, 0, 1);
    float cos_gamma = dot(V, g_SkyPassParameters.m_SunLightDir);
    float gamma = acos(cos_gamma);
    
    float3 Z = g_SkyPassParameters.m_HosekParams.m_Params[9].xyz;
    float3 R = -Z * HosekWilkie(cos_theta, gamma, cos_gamma);
    if (cos_gamma > 0)
    {
		// Only positive values of dot product, so we don't end up creating two
		// spots of light 180 degrees apart
        R = R + pow(float3(cos_gamma, cos_gamma, cos_gamma), float3(256, 256, 256)) * 0.5;
    }
    
    outColor = float4(R, 1.0f);
}
