#ifndef _SHADOWFILTERING_HLSL_
#define _SHADOWFILTERING_HLSL_

struct ShadowFilteringParams
{
    float3 m_WorldPosition;
    float3 m_CameraPosition;
    float4 m_CSMDistances;
    float4x4 m_DirLightViewProj[4];
    float m_InvShadowMapResolution;
    
    Texture2DArray m_DirLightShadowDepthTexture;
    sampler m_PointClampSampler;
    SamplerComparisonState m_PointComparisonLessSampler;
    SamplerComparisonState m_LinearComparisonLessSampler;
};

float Manual1x1PCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    return params.m_DirLightShadowDepthTexture.SampleCmpLevelZero(params.m_PointComparisonLessSampler, float3(shadowTexCoord.xy, CSMIndex), shadowTexCoord.z);
}

float Manual3X3PCF(ShadowFilteringParams params, float3 shadowTexCoord, uint CSMIndex)
{
    static const float2 kKernal[] =
    {
        float2(-1.0f, -1.0f), float2(0.0f, -1.0f), float2(1.0f, -1.0f),
        float2(-1.0f, 0.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f),
        float2(-1.0f, 1.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f)
    };
    
    const float dx = params.m_InvShadowMapResolution;
    
    float percentLit = 0.0f;
    
    [unroll]
    for (uint i = 0; i < 9; ++i)
    {
        percentLit += Manual1x1PCF(params, shadowTexCoord + float3(kKernal[i] * dx, 0.0f), CSMIndex);
    }
    
    return percentLit / 9.0f;
}

float ShadowFiltering(ShadowFilteringParams params)
{
    float cameraDistance = length(params.m_CameraPosition - params.m_WorldPosition);
    
    // beyond last cascade
    if (cameraDistance >= params.m_CSMDistances.w)
        return 0.0f;
    
    int CSMIndex = 0;
    for (uint i = 0; i < 4; ++i)
    {
        if (cameraDistance < params.m_CSMDistances[i])
        {
            CSMIndex = i;
            break;
        }
    }
    
    float4 shadowTexCoord = mul(float4(params.m_WorldPosition, 1.0f), params.m_DirLightViewProj[CSMIndex]);
    shadowTexCoord.xyz = shadowTexCoord.xyz / shadowTexCoord.w;
    
    // Re-scale to 0-1
    shadowTexCoord.x = (1.0f + shadowTexCoord.x) * 0.5f;
    shadowTexCoord.y = (1.0f - shadowTexCoord.y) * 0.5f;
    
#if 0
    return Manual1x1PCF(params, shadowTexCoord.xyz, CSMIndex);
#elif 1
    return Manual3X3PCF(params, shadowTexCoord.xyz, CSMIndex);
#endif
}

#endif // _SHADOWFILTERING_HLSL_
