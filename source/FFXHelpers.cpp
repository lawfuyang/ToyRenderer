#include "FFXHelpers.h"

#define FFX_CPU
#include "FidelityFX/gpu/ffx_core.h"
#include "FidelityFX/gpu/spd/ffx_spd.h"

#include "Engine.h"
#include "Utilities.h"
#include "SmallVector.h"
#include "Graphic.h"

#include "CommonResources.h"

namespace FFXHelpers
{
	// Constants for SPD dispatches. Must be kept in sync with cbSPD in ffx_spd_callbacks_hlsl.h
	struct SPDConstants
	{
		uint32_t mips;
		uint32_t numWorkGroups;
		uint32_t workGroupOffset[2];
		float    invInputSize[2]; // Only used for linear sampling mode
		float    padding[2];
	};

	void SPD::CreateTransientResources(RenderGraph& renderGraph)
	{
		nvrhi::BufferDesc desc;
		desc.byteSize = sizeof(uint32_t) * 6;
		desc.structStride = sizeof(uint32_t);
		desc.debugName = "SPD Global Atomic Buffer";
		desc.canHaveUAVs = true;

		renderGraph.CreateTransientResource(m_AtomicRDGBufferHandle, desc);
	}

    void SPD::Execute(
		nvrhi::CommandListHandle commandList,
		const RenderGraph& renderGraph,
		nvrhi::TextureHandle srcTex,
		nvrhi::TextureHandle destTex,
		nvrhi::SamplerReductionType reductionType)
	{
		PROFILE_FUNCTION();

		nvrhi::BufferHandle atomicBuffer = renderGraph.GetBuffer(m_AtomicRDGBufferHandle);

		// global atomic counter - MUST be initialized to 0
		commandList->clearBufferUInt(atomicBuffer, 0);

		const nvrhi::TextureDesc& desTexDesc = destTex->getDesc();

		SPDConstants passParameters{};

		// Get SPD info for run
		uint32_t dispatchThreadGroupCountXY[2];
		uint32_t numWorkGroupsAndMips[2];
		uint32_t rectInfo[4] = { 0, 0, desTexDesc.width, desTexDesc.height }; // left, top, width, height
		ffxSpdSetup(dispatchThreadGroupCountXY, passParameters.workGroupOffset, numWorkGroupsAndMips, rectInfo);

		passParameters.mips = numWorkGroupsAndMips[1];
		passParameters.numWorkGroups = numWorkGroupsAndMips[0];

		assert(passParameters.mips == ComputeNbMips(desTexDesc.width, desTexDesc.height));
		assert(passParameters.mips == desTexDesc.mipLevels); // did you set the tex desc 'mipLevels'?

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(SPDConstants)),
			nvrhi::BindingSetItem::Texture_SRV(0, srcTex), // r_input_downsample_src
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, atomicBuffer), // rw_internal_global_atomic
			nvrhi::BindingSetItem::Texture_UAV(1, destTex, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ 6, 1, 0, 1 }), // rw_input_downsample_src_mid_mip
			nvrhi::BindingSetItem::Texture_UAV(2, destTex, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ 0, 1, 0, 1 }) // rw_input_downsample_src_mips[0]
		};

		const uint32_t kStartUAVSlotForMips = 3;

		// fill UAV slots 3-13 for each mip
		uint32_t i = 0;
		for (; i < desTexDesc.mipLevels - 1; ++i)
		{
			bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(kStartUAVSlotForMips + i, destTex, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ i + 1, 1, 0, 1 }));
		}

		// bind dummy uav texture mip lvl beyond desTexDesc.mipLevels
		for (; i < Graphic::kMaxTextureMipsToGenerate; ++i)
		{
			bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(kStartUAVSlotForMips + i, g_CommonResources.DummyUAV2DTexture.m_NVRHITextureHandle));
		}

		auto GetSPDReductionTypeIdx = [](nvrhi::SamplerReductionType reductionType)
			{
				assert(reductionType != nvrhi::SamplerReductionType::Comparison);

				const uint32_t kResults[] =
				{
					0,	        // Standard
					UINT_MAX,	// Comparison
					1,	        // Minimum
					2	        // Maximum
				};

				return kResults[(uint32_t)reductionType];
			};

		const char* kShaderName = StringFormat("ffx_spd_downsample_pass_CS FFX_SPD_OPTION_DOWNSAMPLE_FILTER=%d", GetSPDReductionTypeIdx(reductionType));

		const uint32_t dispatchX = dispatchThreadGroupCountXY[0];
		const uint32_t dispatchY = dispatchThreadGroupCountXY[1];
		g_Graphic.AddComputePass(commandList, kShaderName, bindingSetDesc, Vector3U{ dispatchX, dispatchY, 1 }, &passParameters, sizeof(passParameters));
	}
}
