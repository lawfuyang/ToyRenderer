#pragma once

#include "toyrenderer_common.hlsli"

// Niagara's frustum culling
bool FrustumCull(float3 sphereCenterViewSpace, float radius, float4 frustum)
{
    bool visible = true;
    
	// the left/top/right/bottom plane culling utilizes frustum symmetry to cull against two planes at the same time
    visible &= sphereCenterViewSpace.z * frustum.y + abs(sphereCenterViewSpace.x) * frustum.x < radius;
    visible &= sphereCenterViewSpace.z * frustum.w + abs(sphereCenterViewSpace.y) * frustum.z < radius;
    
	// the near plane culling uses camera space Z directly
    // NOTE: this seems unnecessary?
#if 0
    visible &= (sphereCenterViewSpace.z - radius) < g_GPUCullingPassConstants.m_NearPlane;
#endif
    
    return visible;
}

struct OcclusionCullArguments
{
    float3 m_SphereCenterViewSpace;
    float m_Radius;
    float m_NearPlane;
    float m_P00;
    float m_P11;
    Texture2D m_HZB;
    uint2 m_HZBDimensions;
    SamplerState m_LinearClampMinReductionSampler;
};

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool OcclusionCull(OcclusionCullArguments args)
{
    float3 c = args.m_SphereCenterViewSpace;
    float radius = args.m_Radius;
    float nearPlane = args.m_NearPlane;
    float P00 = args.m_P00;
    float P11 = args.m_P11;
    Texture2D HZB = args.m_HZB;
    uint2 HZBDimensions = args.m_HZBDimensions;
    SamplerState linearClampMinReductionSampler = args.m_LinearClampMinReductionSampler;
    
    // trivially accept if sphere intersects camera near plane
    if ((c.z - nearPlane) < radius)
        return true;
    
    float r = radius;

    float3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    float4 aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    
    aabb.xy = clamp(aabb.xy, -1, 1);
    aabb.zw = clamp(aabb.zw, -1, 1);
    
    // clip space -> uv space
    aabb.xy = ClipXYToUV(aabb.xy);
    aabb.zw = ClipXYToUV(aabb.zw);
    
    float width = (aabb.z - aabb.x) * HZBDimensions.x;
    float height = (aabb.w - aabb.y) * HZBDimensions.y;
    float level = floor(log2(max(width, height)));
    
    // Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
    float depth = HZB.SampleLevel(linearClampMinReductionSampler, (aabb.xy + aabb.zw) * 0.5f, level).x;
    float depthSphere = nearPlane / (c.z - r);

    return depthSphere >= depth;
}

bool ConeCull(float3 sphereCenterViewSpace, float radius, float3 coneAxis, float coneCutoff)
{
    return dot(sphereCenterViewSpace, coneAxis) >= coneCutoff * length(sphereCenterViewSpace) + radius;
}
