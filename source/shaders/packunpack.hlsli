#ifndef _PACKUNPACK_HLSLI_
#define _PACKUNPACK_HLSLI_

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}

float2 PackOctadehron(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 UnpackOctadehron(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

uint PackUnorm2x16(float2 v)
{
	uint2 sv = uint2(round(clamp(v, 0.0, 1.0) * 65535.0));
	return (sv.x | (sv.y << 16u));
}

float2 UnpackUnorm2x16(uint p)
{
	float2 Ret;
	Ret.x = (p & 0xffff) * rcp(65535.0f);
	Ret.y = (p >> 16u) * rcp(65535.0f);
	return Ret;
}

uint PackSnorm2x16(float2 v)
{
	uint2 sv = uint2(round(clamp(v, -1.0, 1.0) * 32767.0) + 32767.0);
	return (sv.x | (sv.y << 16u));
}

float2 UnpackSnorm2x16(uint p)
{
	float2 Ret;
	Ret.x = clamp((float(p & 0xffff) - 32767.0f) * rcp(32767.0f), -1.0, 1.0);
	Ret.y = clamp((float(p >> 16u) - 32767.0f) * rcp(32767.0f), -1.0, 1.0);
	return Ret;
}

uint PackUInt2ToUInt(uint X, uint Y)
{
	return X | (Y << 16u);
}

uint PackUInt2ToUInt(uint2 XY)
{
	return PackUInt2ToUInt(XY.x, XY.y);
}

uint2 UnpackUInt2FromUInt(uint Packed)
{
	return uint2(Packed & 0xffff, Packed >> 16);
}

uint PackFloat2ToUInt(float X, float Y)
{
	return PackUInt2ToUInt(f32tof16(X), f32tof16(Y));
}

uint PackFloat2ToUInt(float2 XY)
{
	return PackFloat2ToUInt(XY.x, XY.y);
}

float2 UnpackFloat2FromUInt(uint In)
{
	return float2(f16tof32(In), f16tof32(In >> 16));
}

uint PackR8(float Value)
{
	return uint(saturate(Value) * 255.0f);
}

float UnpackR8(uint In)
{
	return float(In & 0xFF) * (1.0f / 255.0f);
}

uint PackRGBA8(float4 In)
{
	uint r = (uint(saturate(In.r) * 255.0f) << 0);
	uint g = (uint(saturate(In.g) * 255.0f) << 8);
	uint b = (uint(saturate(In.b) * 255.0f) << 16);
	uint a = (uint(saturate(In.a) * 255.0f) << 24);
	return r | g | b | a;
}

float4 UnpackRGBA8(uint In)
{
	float4 Out;
	Out.r = float((In >> 0) & 0xFF) * (1.0f / 255.0f);
	Out.g = float((In >> 8) & 0xFF) * (1.0f / 255.0f);
	Out.b = float((In >> 16) & 0xFF) * (1.0f / 255.0f);
	Out.a = float((In >> 24) & 0xFF) * (1.0f / 255.0f);
	return Out;
}

uint PackR10G10B10A2F(float4 v)
{
	// Clamp the values to the [-1, 1] range
    v = saturate(v);
	
	// Map from [-1, 1] to [0, 1]
    v = (v + 1.0f) * 0.5f;
	
	// Convert to 10-bit fixed point
    uint x = (uint) (v.x * 1023.0f);
    uint y = (uint) (v.y * 1023.0f);
    uint z = (uint) (v.z * 1023.0f);
	
	// Convert w to 1-bit fixed point
    uint w = (v.w >= 0.0f) ? 1 : 0;
	
	// Pack the values into a single uint
    return (w << 30) | (z << 20) | (y << 10) | x;
}

float4 UnpackR10G10B10A2F(uint packed)
{
    // Extract each component
    uint xInt = (packed >> 20) & 0x3FF; // 10 bits for x
    uint yInt = (packed >> 10) & 0x3FF; // 10 bits for y
    uint zInt = packed & 0x3FF; // 10 bits for z
    uint wInt = (packed >> 30) & 0x1; // 1 bit for w

    // Convert back to [0, 1] by dividing by 1023
    float x = (float) xInt / 1023.0f;
    float y = (float) yInt / 1023.0f;
    float z = (float) zInt / 1023.0f;

    // Map from [0, 1] back to [-1, 1]
    x = (x * 2.0f) - 1.0f;
    y = (y * 2.0f) - 1.0f;
    z = (z * 2.0f) - 1.0f;

    // Unpack w, convert to either 1 or -1
    float w = (wInt == 1) ? 1.0f : -1.0f;

    return float4(x, y, z, w);
}

uint Pack10F(float Value)
{
	return (f32tof16(Value) >> 5) & 0x000003FF;
}

float Unpack10F(uint Value)
{
	return f16tof32((Value << 5) & 0x7FE0);
}

uint PackR11G11B10F(float3 rgb)
{
	uint r = (f32tof16(rgb.r) << 17) & 0xFFE00000;
	uint g = (f32tof16(rgb.g) << 6) & 0x001FFC00;
	uint b = (f32tof16(rgb.b) >> 5) & 0x000003FF;
	return r | g | b;
}

float3 UnpackR11G11B10F(uint rgb)
{
	float r = f16tof32((rgb >> 17) & 0x7FF0);
	float g = f16tof32((rgb >> 6) & 0x7FF0);
	float b = f16tof32((rgb << 5) & 0x7FE0);
	return float3(r, g, b);
}

uint PackR10G10B10F(float3 rgb)
{
	uint r = (f32tof16(rgb.r) << 15) & 0x3FF00000;	// 0011 1111 1111 0000 0000 0000 0000 0000 
	uint g = (f32tof16(rgb.g) << 5) & 0x000FFC00;	// 0000 0000 0000 1111 1111 1100 0000 0000 
	uint b = (f32tof16(rgb.b) >> 5) & 0x000003FF;	// 0000 0000 0000 0000 0000 0011 1111 1111
	return r | g | b;
}

float3 UnpackR10G10B10F(uint rgb)
{
	float r = f16tof32((rgb >> 15) & 0x7FE0);
	float g = f16tof32((rgb >> 5) & 0x7FE0);
	float b = f16tof32((rgb << 5) & 0x7FE0);
	return float3(r, g, b);
}

uint2 PackR16G16B16A16F(float4 In)
{
	return uint2(PackFloat2ToUInt(In.xy), PackFloat2ToUInt(In.zw));
}

float4 UnpackR16G16B16A16F(uint2 In)
{
	return float4(UnpackFloat2FromUInt(In.x), UnpackFloat2FromUInt(In.y));
}

uint PackR24F(float In)
{
	return asuint(In) >> 8;
}

float UnpackR24F(uint In)
{
    return asfloat(In << 8);
}

uint PackR9G9B9E5(float3 value)
{
	// To determine the shared exponent, we must clamp the channels to an expressible range
    const float kMaxVal = asfloat(0x477F8000); // 1.FF x 2^+15
    const float kMinVal = asfloat(0x37800000); // 1.00 x 2^-16

	// Non-negative and <= kMaxVal
    value = clamp(value, 0, kMaxVal);

	// From the maximum channel we will determine the exponent.  We clamp to a min value
	// so that the exponent is within the valid 5-bit range.
    float MaxChannel = max(max(kMinVal, value.r), max(value.g, value.b));

	// 'Bias' has to have the biggest exponent plus 15 (and nothing in the mantissa).  When
	// added to the three channels, it shifts the explicit '1' and the 8 most significant
	// mantissa bits into the low 9 bits.  IEEE rules of float addition will round rather
	// than truncate the discarded bits.  Channels with smaller natural exponents will be
	// shifted further to the right (discarding more bits).
    float Bias = asfloat((asuint(MaxChannel) + 0x07804000) & 0x7F800000);

	// Shift bits into the right places
    uint3 RGB = asuint(value + Bias);
    uint E = (asuint(Bias) << 4) + 0x10000000;
    return E | RGB.b << 18 | RGB.g << 9 | (RGB.r & 0x1FF);
}

float3 UnpackR9G9B9E5(uint v)
{
    float3 rgb = uint3(v, v >> 9, v >> 18) & 0x1FF;
    return ldexp(rgb, (int) (v >> 27) - 24);
}

#endif // _PACKUNPACK_HLSLI_
