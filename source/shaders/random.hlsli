#ifndef _RANDOM_H_
#define _RANDOM_H_

#include "toyrenderer_common.hlsli"

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float QuickRandomFloat(inout uint seed)
{
    seed = (1664525u * seed + 1013904223u);
    return float(seed & 0x00FFFFFF) / float(0x01000000);
}

// From Nathan Reed's blog at:
// http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
uint WangHash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

uint Xorshift(uint seed)
{
    // Xorshift algorithm from George Marsaglia's paper
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);
    return seed;
}

uint GetRandomUInt(inout uint seed)
{
    seed = WangHash(seed);
    return Xorshift(seed);
}

float GetRandomFloat(inout uint seed)
{
    return ((float)GetRandomUInt(seed)) * (1.f / 4294967296.0f);
}

// 3D value noise
// Ref: https://www.shadertoy.com/view/XsXfRH
float Hash(float3 p)
{
    p = frac(p * 0.3183099 + .1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

//From "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//http://advances.realtimerendering.com/s2014/index.html
float InterleavedGradientNoise(float2 uv)
{
    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(uv, magic.xy)));
}

float InterleavedGradientNoise(float2 uv, float offset)
{
    uv += offset * (float2(47, 17) * 0.695f);
    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(uv, magic.xy)));
}

// https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float InterleavedGradientNoise(int x, int y) {
    return fmod(52.9829189f * fmod(0.06711056f * x + 0.00583715f * y, 1.0f), 1.0f);
}

/**
* Compute a uniformly distributed random direction on the hemisphere about the given (normal) direction.
*/
float3 GetRandomDirectionOnHemisphere(float3 direction, inout uint seed)
{
    float3 p;
    do
    {
        p.x = GetRandomFloat(seed) * 2.f - 1.f;
        p.y = GetRandomFloat(seed) * 2.f - 1.f;
        p.z = GetRandomFloat(seed) * 2.f - 1.f;

        // Only accept unit length directions to stay inside
        // the unit sphere and be uniformly distributed
    } while (length(p) > 1.f);

    // Direction is on the opposite hemisphere, flip and use it
    if (dot(direction, p) < 0.f)
        p *= -1.f;
    return normalize(p);
}

/**
* Compute a cosine distributed random direction on the hemisphere about the given (normal) direction.
*/
float3 GetRandomCosineDirectionOnHemisphere(float3 direction, inout uint seed)
{
    // Choose random points on the unit sphere offset along the surface normal
    // to produce a cosine distribution of random directions.
    float a = GetRandomFloat(seed) * M_PI * 2.0f;
    float z = GetRandomFloat(seed) * 2.f - 1.f;
    float r = sqrt(1.f - z * z);

    float3 p = float3(r * cos(a), r * sin(a), z) + direction;
    return normalize(p);
}

#endif // _RANDOM_H_
