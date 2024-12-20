#include "common.hlsli"
#include "lightingcommon.hlsli"

#include "shared/AmbientOcclusionStructs.h"

#define XE_GTAO_USE_HALF_FLOAT_PRECISION 1
//#define XE_GTAO_FP32_DEPTHS
//#define XE_GTAO_COMPUTE_BENT_NORMALS

#if (DEBUG_OUTPUT_MODE == 1)
    #define XE_GTAO_SHOW_NORMALS
#elif (DEBUG_OUTPUT_MODE == 2)
    #define XE_GTAO_SHOW_EDGES
#elif (DEBUG_OUTPUT_MODE == 3) && (XE_GTAO_COMPUTE_BENT_NORMALS)
    #define XE_GTAO_SHOW_BENT_NORMALS
#endif

#include "extern/xegtao/XeGTAO.hlsli"

cbuffer GTAOConstantBuffer : register(b0) { GTAOConstants g_GTAOConsts; }
cbuffer XeGTAOMainPassConstantBuffer : register(b1){ XeGTAOMainPassConstantBuffer g_XeGTAOMainPassConstantBuffer; }
cbuffer XeGTAODenoiseConstantBuffer : register(b1) { XeGTAODenoiseConstants g_XeGTAODenoiseConstants; }

// input output textures for the first pass (XeGTAO_PrefilterDepths16x16)
Texture2D<float> g_srcRawDepth : register(t0); // source depth buffer data (in NDC space in DirectX)
RWTexture2D<lpfloat> g_outWorkingDepthMIP0 : register(u0); // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat> g_outWorkingDepthMIP1 : register(u1); // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat> g_outWorkingDepthMIP2 : register(u2); // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat> g_outWorkingDepthMIP3 : register(u3); // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat> g_outWorkingDepthMIP4 : register(u4); // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)

// input output textures for the second pass (XeGTAO_MainPass)
Texture2D<lpfloat> g_srcWorkingDepth : register(t0); // viewspace depth with MIPs, output by XeGTAO_PrefilterDepths16x16 and consumed by XeGTAO_MainPass
Texture2D<uint> g_srcHilbertLUT : register(t1); // hilbert lookup table
Texture2D<uint4> g_GBufferA : register(t2);
RWTexture2D<uint> g_outWorkingAOTerm : register(u0); // output AO term (includes bent normals if enabled - packed as R11G11B10 scaled by AO)
RWTexture2D<unorm float> g_outWorkingEdges : register(u1); // output depth-based edges used by the denoiser

// input output textures for the third pass (XeGTAO_Denoise)
Texture2D<uint> g_srcWorkingAOTerm : register(t0); // coming from previous pass
Texture2D<lpfloat> g_srcWorkingEdges : register(t1); // coming from previous pass
RWTexture2D<uint> g_outFinalAOTerm : register(u0); // final AO term - just 'visibility' or 'visibility + bent normals'

sampler g_PointClampSampler : register(s0);

[numthreads(8, 8, 1)] // <- hard coded to 8x8; each thread computes 2x2 blocks so processing 16x16 block: Dispatch needs to be called with (width + 16-1) / 16, (height + 16-1) / 16
void CS_XeGTAO_PrefilterDepths(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    XeGTAO_PrefilterDepths16x16(dispatchThreadID.xy, groupThreadID.xy, g_GTAOConsts, g_srcRawDepth, g_PointClampSampler, g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2, g_outWorkingDepthMIP3, g_outWorkingDepthMIP4);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_X, 1)]
void CS_XeGTAO_MainPass(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    lpfloat sliceCount = 1.0f;
    lpfloat stepsPerSlice = 2.0f;
    
    switch (g_XeGTAOMainPassConstantBuffer.m_Quality)
    {
        // Low
        case 0:
            sliceCount = 1.0f;
            stepsPerSlice = 2.0f;
            break;
        
        // Medium
        case 1:
            sliceCount = 2.0f;
            stepsPerSlice = 2.0f;
            break;
        
        // High
        case 2:
            sliceCount = 3.0f;
            stepsPerSlice = 3.0f;
            break;
        
        // Ultra
        case 3:
            sliceCount = 9.0f;
            stepsPerSlice = 2.0f;
            break;
    };
    
    uint noiseIndex = g_srcHilbertLUT.Load(uint3(dispatchThreadID.xy % 64, 0)).x;
    
    // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
    // NOTE: without TAA, temporalIndex is always 0
    noiseIndex += 288 * (g_GTAOConsts.NoiseIndex % 64);
    
    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    float2 spatioTemporalNoise = lpfloat2(frac(0.5f + noiseIndex * float2(0.75487766624669276005, 0.5698402909980532659114)));
    
    // compute view space normals for XeGTAO input
    // NOTE: this assumes AO pass is full render resolution
    GBufferParams gbufferParams;
    UnpackGBuffer(g_GBufferA[dispatchThreadID.xy], gbufferParams);
    float3 viewSpaceNormals = mul(float4(gbufferParams.m_Normal, 1.0f), g_XeGTAOMainPassConstantBuffer.m_ViewMatrixNoTranslate).xyz;
    
    // XeGTAO follows LHS, so we need to flip Z
    viewSpaceNormals.z *= -1.0f;
    
    XeGTAO_MainPass(dispatchThreadID.xy, sliceCount, stepsPerSlice, (lpfloat2)spatioTemporalNoise, (lpfloat3)viewSpaceNormals, g_GTAOConsts, g_srcWorkingDepth, g_PointClampSampler, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_X, 1)]
void CS_XeGTAO_Denoise(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint2 pixCoordBase = dispatchThreadID.xy * uint2(2, 1); // we're computing 2 horizontal pixels at a time (performance optimization)
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_PointClampSampler, g_outFinalAOTerm, g_XeGTAODenoiseConstants.m_FinalApply);
}
