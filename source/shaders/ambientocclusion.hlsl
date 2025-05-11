#include "toyrenderer_common.hlsli"
#include "lightingcommon.hlsli"

#include "ShaderInterop.h"

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
cbuffer XeGTAOPrefilterDepthsResourceIndicesBuffer : register(b1) { XeGTAOPrefilterDepthsResourceIndices g_XeGTAOPrefilterDepthsResourceIndices; }
cbuffer XeGTAOMainPassConstantBuffer : register(b1) { XeGTAOMainPassConstantBuffer g_XeGTAOMainPassConstantBuffer; }
cbuffer XeGTAODenoiseConstantBuffer : register(b1) { XeGTAODenoiseConstants g_XeGTAODenoiseConstants; }

[numthreads(8, 8, 1)] // <- hard coded to 8x8; each thread computes 2x2 blocks so processing 16x16 block: Dispatch needs to be called with (width + 16-1) / 16, (height + 16-1) / 16
void CS_XeGTAO_PrefilterDepths(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    Texture2D<float> srcRawDepth = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_SrcRawDepthIdx]; // source depth buffer data (in NDC space in DirectX)
    RWTexture2D<lpfloat> outWorkingDepthMIP0 = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_OutWorkingDepthMIP0Idx]; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    RWTexture2D<lpfloat> outWorkingDepthMIP1 = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_OutWorkingDepthMIP1Idx]; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    RWTexture2D<lpfloat> outWorkingDepthMIP2 = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_OutWorkingDepthMIP2Idx]; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    RWTexture2D<lpfloat> outWorkingDepthMIP3 = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_OutWorkingDepthMIP3Idx]; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    RWTexture2D<lpfloat> outWorkingDepthMIP4 = ResourceDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_OutWorkingDepthMIP4Idx]; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    sampler pointClampSampler = SamplerDescriptorHeap[g_XeGTAOPrefilterDepthsResourceIndices.m_PointClampSamplerIdx];
    
    XeGTAO_PrefilterDepths16x16(dispatchThreadID.xy, groupThreadID.xy, g_GTAOConsts, srcRawDepth, pointClampSampler, outWorkingDepthMIP0, outWorkingDepthMIP1, outWorkingDepthMIP2, outWorkingDepthMIP3, outWorkingDepthMIP4);
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
    
    Texture2D<lpfloat> srcWorkingDepth = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_SrcWorkingDepthIdx]; // viewspace depth with MIPs, output by XeGTAO_PrefilterDepths16x16 and consumed by XeGTAO_MainPass
    Texture2D<uint> srcHilbertLUT = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_SrcHilbertLUTIdx]; // hilbert lookup table
    Texture2D<uint4> GBufferA = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_GBufferAIdx];
    RWTexture2D<uint> outWorkingAOTerm = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_OutWorkingAOTermIdx]; // output AO term (includes bent normals if enabled - packed as R11G11B10 scaled by AO)
    RWTexture2D<unorm float> outWorkingEdges = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_OutWorkingEdgesIdx]; // output depth-based edges used by the denoiser
    RWTexture2D<float4> outputDbgImage = ResourceDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_DebugOutputIdx]; // debug image output (if enabled)
    sampler pointClampSampler = SamplerDescriptorHeap[g_XeGTAOMainPassConstantBuffer.m_PointClampSamplerIdx];
    
    uint noiseIndex = srcHilbertLUT.Load(uint3(dispatchThreadID.xy % 64, 0)).x;
    
    // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
    // NOTE: without TAA, temporalIndex is always 0
    noiseIndex += 288 * (g_GTAOConsts.NoiseIndex % 64);
    
    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    float2 spatioTemporalNoise = lpfloat2(frac(0.5f + noiseIndex * float2(0.75487766624669276005, 0.5698402909980532659114)));
    
    // compute view space normals for XeGTAO input
    // NOTE: this assumes AO pass is full render resolution
    GBufferParams gbufferParams;
    UnpackGBuffer(GBufferA[dispatchThreadID.xy], gbufferParams);
    float3 viewSpaceNormals = mul(float4(gbufferParams.m_Normal, 1.0f), g_XeGTAOMainPassConstantBuffer.m_WorldToViewNoTranslate).xyz;
    viewSpaceNormals.z *= -1.0f; // TODO: fix inverted view-space Z coord
    
    XeGTAO_MainPass(dispatchThreadID.xy, sliceCount, stepsPerSlice, (lpfloat2)spatioTemporalNoise, (lpfloat3)viewSpaceNormals, g_GTAOConsts, srcWorkingDepth, pointClampSampler, outWorkingAOTerm, outWorkingEdges, outputDbgImage);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_X, 1)]
void CS_XeGTAO_Denoise(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    Texture2D<uint> srcWorkingAOTerm = ResourceDescriptorHeap[g_XeGTAODenoiseConstants.m_SrcWorkingAOTermIdx]; // coming from previous pass
    Texture2D<lpfloat> srcWorkingEdges = ResourceDescriptorHeap[g_XeGTAODenoiseConstants.m_SrcWorkingEdgesIdx]; // coming from previous pass
    RWTexture2D<uint> outFinalAOTerm = ResourceDescriptorHeap[g_XeGTAODenoiseConstants.m_OutFinalAOTermIdx]; // final AO term - just 'visibility' or 'visibility + bent normals'
    RWTexture2D<float4> outputDbgImage = ResourceDescriptorHeap[g_XeGTAODenoiseConstants.m_DebugOutputIdx]; // debug image output (if enabled)
    
    const uint2 pixCoordBase = dispatchThreadID.xy * uint2(2, 1); // we're computing 2 horizontal pixels at a time (performance optimization)
    sampler pointClampSampler = SamplerDescriptorHeap[g_XeGTAODenoiseConstants.m_PointClampSamplerIdx];
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, srcWorkingAOTerm, srcWorkingEdges, pointClampSampler, outFinalAOTerm, g_XeGTAODenoiseConstants.m_FinalApply, outputDbgImage);
}
