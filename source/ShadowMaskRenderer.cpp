#include "Graphic.h"

#include "extern/nvidia/NRD/Include/NRD.h"

#include "CommonResources.h"
#include "Engine.h"
#include "FFXHelpers.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/ShadowMaskStructs.h"
#include "shaders/shared/MinMaxDownsampleStructs.h"

#define NRD_CALL(fn) if (nrd::Result result = fn; result != nrd::Result::SUCCESS) { LOG_DEBUG("NRD call failed: %s", EnumUtils::ToString(result)); assert(0); }
#define NRD_ID(x) nrd::Identifier(nrd::Denoiser::x)

RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
RenderGraph::ResourceHandle g_LinearViewDepthRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;

static nvrhi::Format GetNVRHIFormat(nrd::Format format)
{
	switch (format)
	{
	case nrd::Format::R8_UNORM:             return nvrhi::Format::R8_UNORM;
	case nrd::Format::R8_SNORM:             return nvrhi::Format::R8_SNORM;
	case nrd::Format::R8_UINT:              return nvrhi::Format::R8_UINT;
	case nrd::Format::R8_SINT:              return nvrhi::Format::R8_SINT;
	case nrd::Format::RG8_UNORM:            return nvrhi::Format::RG8_UNORM;
	case nrd::Format::RG8_SNORM:            return nvrhi::Format::RG8_SNORM;
	case nrd::Format::RG8_UINT:             return nvrhi::Format::RG8_UINT;
	case nrd::Format::RG8_SINT:             return nvrhi::Format::RG8_SINT;
	case nrd::Format::RGBA8_UNORM:          return nvrhi::Format::RGBA8_UNORM;
	case nrd::Format::RGBA8_SNORM:          return nvrhi::Format::RGBA8_SNORM;
	case nrd::Format::RGBA8_UINT:           return nvrhi::Format::RGBA8_UINT;
	case nrd::Format::RGBA8_SINT:           return nvrhi::Format::RGBA8_SINT;
	case nrd::Format::RGBA8_SRGB:           return nvrhi::Format::SRGBA8_UNORM;
	case nrd::Format::R16_UNORM:            return nvrhi::Format::R16_UNORM;
	case nrd::Format::R16_SNORM:            return nvrhi::Format::R16_SNORM;
	case nrd::Format::R16_UINT:             return nvrhi::Format::R16_UINT;
	case nrd::Format::R16_SINT:             return nvrhi::Format::R16_SINT;
	case nrd::Format::R16_SFLOAT:           return nvrhi::Format::R16_FLOAT;
	case nrd::Format::RG16_UNORM:           return nvrhi::Format::RG16_UNORM;
	case nrd::Format::RG16_SNORM:           return nvrhi::Format::RG16_SNORM;
	case nrd::Format::RG16_UINT:            return nvrhi::Format::RG16_UINT;
	case nrd::Format::RG16_SINT:            return nvrhi::Format::RG16_SINT;
	case nrd::Format::RG16_SFLOAT:          return nvrhi::Format::RG16_FLOAT;
	case nrd::Format::RGBA16_UNORM:         return nvrhi::Format::RGBA16_UNORM;
	case nrd::Format::RGBA16_SNORM:         return nvrhi::Format::RGBA16_SNORM;
	case nrd::Format::RGBA16_UINT:          return nvrhi::Format::RGBA16_UINT;
	case nrd::Format::RGBA16_SINT:          return nvrhi::Format::RGBA16_SINT;
	case nrd::Format::RGBA16_SFLOAT:        return nvrhi::Format::RGBA16_FLOAT;
	case nrd::Format::R32_UINT:             return nvrhi::Format::R32_UINT;
	case nrd::Format::R32_SINT:             return nvrhi::Format::R32_SINT;
	case nrd::Format::R32_SFLOAT:           return nvrhi::Format::R32_FLOAT;
	case nrd::Format::RG32_UINT:            return nvrhi::Format::RG32_UINT;
	case nrd::Format::RG32_SINT:            return nvrhi::Format::RG32_SINT;
	case nrd::Format::RG32_SFLOAT:          return nvrhi::Format::RG32_FLOAT;
	case nrd::Format::RGB32_UINT:           return nvrhi::Format::RGB32_UINT;
	case nrd::Format::RGB32_SINT:           return nvrhi::Format::RGB32_SINT;
	case nrd::Format::RGB32_SFLOAT:         return nvrhi::Format::RGB32_FLOAT;
	case nrd::Format::RGBA32_UINT:          return nvrhi::Format::RGBA32_UINT;
	case nrd::Format::RGBA32_SINT:          return nvrhi::Format::RGBA32_SINT;
	case nrd::Format::RGBA32_SFLOAT:        return nvrhi::Format::RGBA32_FLOAT;
	case nrd::Format::R10_G10_B10_A2_UNORM: return nvrhi::Format::R10G10B10A2_UNORM;
	case nrd::Format::R10_G10_B10_A2_UINT:  return nvrhi::Format::UNKNOWN; // not representable and not used
	case nrd::Format::R11_G11_B10_UFLOAT:   return nvrhi::Format::R11G11B10_FLOAT;
	case nrd::Format::R9_G9_B9_E5_UFLOAT:   return nvrhi::Format::UNKNOWN; // not representable and not used
	default:                                return nvrhi::Format::UNKNOWN;
	}
}

class ShadowMaskRenderer : public IRenderer
{
	nrd::Instance* m_NRDInstance = nullptr;
	nvrhi::BufferHandle m_NRDConstantBuffer;
    nvrhi::SamplerHandle m_NRDSamplers[(uint32_t)nrd::Sampler::MAX_NUM];
	std::vector<nvrhi::TextureDesc> m_NRDTemporaryTextureDescs;
	std::vector<nvrhi::TextureHandle> m_NRDPermanentTextures;
    std::vector<RenderGraph::ResourceHandle> m_NRDTemporaryTextureHandles;

public:
	ShadowMaskRenderer() : IRenderer("ShadowMaskRenderer") {}

	~ShadowMaskRenderer()
	{
		if (m_NRDInstance)
		{
			nrd::DestroyInstance(*m_NRDInstance);
			m_NRDInstance = nullptr;
		}
	}

    void Initialize() override
    {
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        const nrd::DenoiserDesc denoiserDescs[] = { NRD_ID(SIGMA_SHADOW), nrd::Denoiser::SIGMA_SHADOW };

        nrd::InstanceCreationDesc instanceCreationDesc{};
		instanceCreationDesc.denoisers = denoiserDescs;
		instanceCreationDesc.denoisersNum = std::size(denoiserDescs);

		NRD_CALL(nrd::CreateInstance(instanceCreationDesc, m_NRDInstance));

		const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_NRDInstance);

		const nvrhi::BufferDesc constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(instanceDesc.constantBufferMaxDataSize, "NrdConstantBuffer", 1);
		m_NRDConstantBuffer = device->createBuffer(constantBufferDesc);

		assert(instanceDesc.samplersNum == std::size(m_NRDSamplers));
		for (uint32_t i = 0; i < std::size(m_NRDSamplers); ++i)
		{
			const nrd::Sampler& samplerMode = instanceDesc.samplers[i];

			switch (samplerMode)
			{
			case nrd::Sampler::NEAREST_CLAMP:
				m_NRDSamplers[i] = g_CommonResources.PointClampSampler;
				break;
			case nrd::Sampler::LINEAR_CLAMP:
				m_NRDSamplers[i] = g_CommonResources.LinearClampSampler;
				break;
			default:
				assert(!"Unknown NRD sampler mode");
				break;
			}
		}

        m_NRDTemporaryTextureHandles.resize(instanceDesc.transientPoolSize);

		const uint32_t poolSize = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

		for (uint32_t i = 0; i < poolSize; i++)
		{
			const bool bIsPermanent = (i < instanceDesc.permanentPoolSize);

			const nrd::TextureDesc& nrdTextureDesc = bIsPermanent
				? instanceDesc.permanentPool[i]
				: instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];

			const nvrhi::Format format = GetNVRHIFormat(nrdTextureDesc.format);
			assert(format != nvrhi::Format::UNKNOWN);

			nvrhi::TextureDesc textureDesc;
			textureDesc.width = DivideAndRoundUp(g_Graphic.m_RenderResolution.x, nrdTextureDesc.downsampleFactor);
			textureDesc.height = DivideAndRoundUp(g_Graphic.m_RenderResolution.y, nrdTextureDesc.downsampleFactor);
			textureDesc.format = format;
			textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
			textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			textureDesc.isUAV = true;
			textureDesc.debugName = StringFormat("NRD %s Texture [%d]", bIsPermanent ? "Permanent" : "Transient", i);

			if (bIsPermanent)
			{
				m_NRDPermanentTextures.push_back(device->createTexture(textureDesc));
			}
			else
			{
				m_NRDTemporaryTextureDescs.push_back(textureDesc);
			}
		}
    }

	bool Setup(RenderGraph& renderGraph) override
    {
		if (!g_Scene->IsShadowsEnabled())
		{
			return false;
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = g_Graphic.m_RenderResolution.x;
			desc.height = g_Graphic.m_RenderResolution.y;
			desc.format = nvrhi::Format::R16_FLOAT;
			desc.debugName = "Shadow Mask Texture";
			desc.isUAV = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			renderGraph.CreateTransientResource(g_ShadowMaskRDGTextureHandle, desc);
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = g_Graphic.m_RenderResolution.x;
			desc.height = g_Graphic.m_RenderResolution.y;
            desc.format = nvrhi::Format::R16_FLOAT;
            desc.debugName = "Linear View Depth";
			desc.isUAV = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(g_LinearViewDepthRDGTextureHandle, desc);
		}

		for (uint32_t i = 0; i < m_NRDTemporaryTextureDescs.size(); ++i)
		{
			renderGraph.CreateTransientResource(m_NRDTemporaryTextureHandles[i], m_NRDTemporaryTextureDescs[i]);
		}

		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);

        return true;
    }

	void TraceShadows(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
	{
        PROFILE_GPU_SCOPED(commandList, "TraceShadows");

		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		View& view = g_Scene->m_View;

		const auto& controllables = g_GraphicPropertyGrid.m_ShadowControllables;

		nvrhi::TextureHandle shadowMaskTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
        nvrhi::TextureHandle linearViewDepthTexture = renderGraph.GetTexture(g_LinearViewDepthRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
		nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

		const float tanSunAngularRadius = tanf(ConvertToRadians(controllables.m_SunAngularDiameter * 0.5f));

		ShadowMaskConsts passConstants;
		passConstants.m_ClipToWorld = view.m_ClipToWorld;
		passConstants.m_DirectionalLightDirection = g_Scene->m_DirLightVec;
		passConstants.m_OutputResolution = Vector2U{ shadowMaskTexture->getDesc().width , shadowMaskTexture->getDesc().height };
		passConstants.m_NoisePhase = (g_Graphic.m_FrameCounter & 0xff) * kGoldenRatio;
		passConstants.m_TanSunAngularRadius = controllables.m_bEnableSoftShadows ? tanSunAngularRadius : 0.0f;
        passConstants.m_CameraPosition = g_Scene->m_View.m_Eye;
		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopy),
			nvrhi::BindingSetItem::RayTracingAccelStruct(1, g_Scene->m_TLAS),
			nvrhi::BindingSetItem::Texture_SRV(2, GBufferATexture),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Scene->m_InstanceConstsBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, g_Graphic.m_GlobalVertexBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Graphic.m_GlobalMaterialDataBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalIndexBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(7, g_Graphic.m_GlobalMeshDataBuffer),
			nvrhi::BindingSetItem::Texture_SRV(8, g_CommonResources.BlueNoise.m_NVRHITextureHandle),
			nvrhi::BindingSetItem::Texture_UAV(0, shadowMaskTexture),
            nvrhi::BindingSetItem::Texture_UAV(1, linearViewDepthTexture),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler),
		};

		Graphic::ComputePassParams computePassParams;
		computePassParams.m_CommandList = commandList;
		computePassParams.m_ShaderName = "shadowmask_CS_ShadowMask";
		computePassParams.m_BindingSetDesc = bindingSetDesc;
		computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(passConstants.m_OutputResolution, 8);
		computePassParams.m_ShouldAddBindlessResources = true;

		g_Graphic.AddComputePass(computePassParams);
	}

	void DenoiseShadows(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
	{
		PROFILE_GPU_SCOPED(commandList, "Denoise Shadows");

		View& view = g_Scene->m_View;

		const auto& controllables = g_GraphicPropertyGrid.m_ShadowControllables;

		nrd::SigmaSettings sigmaSettings;
		memcpy(sigmaSettings.lightDirection, &g_Scene->m_DirLightVec, sizeof(sigmaSettings.lightDirection));
		nrd::SetDenoiserSettings(*m_NRDInstance, 0, &sigmaSettings);

		nrd::CommonSettings commonSettings;
        memcpy(commonSettings.viewToClipMatrix, &view.m_ViewToClip, sizeof(commonSettings.viewToClipMatrix));
        memcpy(commonSettings.viewToClipMatrixPrev, &view.m_PrevViewToClip, sizeof(commonSettings.viewToClipMatrixPrev));
        memcpy(commonSettings.worldToViewMatrix, &view.m_WorldToView, sizeof(commonSettings.worldToViewMatrix));
        memcpy(commonSettings.worldToViewMatrixPrev, &view.m_PrevWorldToView, sizeof(commonSettings.worldToViewMatrixPrev));
        commonSettings.motionVectorScale[0] = 1.0f / g_Graphic.m_RenderResolution.x;
        commonSettings.motionVectorScale[1] = 1.0f / g_Graphic.m_RenderResolution.y;
        commonSettings.cameraJitter[0] = 0.0f; // TODO: jitter stuff
        commonSettings.cameraJitter[1] = 0.0f;
		commonSettings.cameraJitterPrev[0] = 0.0f;
		commonSettings.cameraJitterPrev[1] = 0.0f;
		commonSettings.resourceSize[0] = g_Graphic.m_RenderResolution.x;
		commonSettings.resourceSize[1] = g_Graphic.m_RenderResolution.y;
        commonSettings.resourceSizePrev[0] = g_Graphic.m_RenderResolution.x;
        commonSettings.resourceSizePrev[1] = g_Graphic.m_RenderResolution.y;
        commonSettings.rectSize[0] = g_Graphic.m_DisplayResolution.x;
        commonSettings.rectSize[1] = g_Graphic.m_DisplayResolution.y;
        commonSettings.rectSizePrev[0] = g_Graphic.m_DisplayResolution.x;
        commonSettings.rectSizePrev[1] = g_Graphic.m_DisplayResolution.y;
		commonSettings.denoisingRange = g_Scene->m_BoundingSphere.Radius * 2;
		commonSettings.splitScreen = controllables.m_DenoiseSplitScreenSlider;
		commonSettings.frameIndex = g_Graphic.m_FrameCounter;
		commonSettings.accumulationMode = nrd::AccumulationMode::CONTINUE; // TODO: change when camera resets or jumps
		commonSettings.isMotionVectorInWorldSpace = false; // TODO: Motion Vectors input
		commonSettings.enableValidation = false; // NOTE: not used for SIGMA denoising

		NRD_CALL(nrd::SetCommonSettings(*m_NRDInstance, commonSettings));

        const nrd::Identifier denoiserIdentifiers[] = { NRD_ID(SIGMA_SHADOW) };
		const nrd::DispatchDesc* dispatchDescs = nullptr;
		uint32_t dispatchDescNum = 0;
		nrd::GetComputeDispatches(*m_NRDInstance, denoiserIdentifiers, std::size(denoiserIdentifiers), dispatchDescs, dispatchDescNum);

		std::vector<nvrhi::TextureHandle> transientTextures;
        transientTextures.resize(m_NRDTemporaryTextureHandles.size());

        for (uint32_t i = 0; i < m_NRDTemporaryTextureHandles.size(); ++i)
        {
            transientTextures[i] = renderGraph.GetTexture(m_NRDTemporaryTextureHandles[i]);
        }

		nvrhi::TextureHandle linearViewDepthTexture = renderGraph.GetTexture(g_LinearViewDepthRDGTextureHandle);

		const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_NRDInstance);

		for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchDescNum; dispatchIndex++)
		{
			const nrd::DispatchDesc& dispatchDesc = dispatchDescs[dispatchIndex];

			PROFILE_GPU_SCOPED(commandList, dispatchDesc.name ? dispatchDesc.name : "Dispatch");

			const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];

			if (dispatchDesc.constantBufferDataSize)
			{
				commandList->writeBuffer(m_NRDConstantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);
			}

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(instanceDesc.constantBufferRegisterIndex, m_NRDConstantBuffer));

            assert(instanceDesc.samplersNum <= std::size(m_NRDSamplers));
			for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc.samplersNum; samplerIndex++)
			{
				bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(instanceDesc.samplersBaseRegisterIndex + samplerIndex, m_NRDSamplers[samplerIndex]));
			}

			uint32_t resourceIndex = 0;
			for (uint32_t resourceRangeIndex = 0; resourceRangeIndex < nrdPipelineDesc.resourceRangesNum; resourceRangeIndex++)
			{
				const nrd::ResourceRangeDesc& nrdDescriptorRange = nrdPipelineDesc.resourceRanges[resourceRangeIndex];

				for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorsNum; descriptorOffset++)
				{
					assert(resourceIndex < dispatchDesc.resourcesNum);
					const nrd::ResourceDesc& resource = dispatchDesc.resources[resourceIndex];

					nvrhi::TextureHandle texture;
					switch (resource.type)
					{
					case nrd::ResourceType::IN_MV:
						texture = g_CommonResources.BlackTexture.m_NVRHITextureHandle; // TODO: Motion Vectors input
						break;
					case nrd::ResourceType::IN_VIEWZ:
						texture = linearViewDepthTexture;
						break;
					case nrd::ResourceType::IN_PENUMBRA:
						//texture = 
						break;
					case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
						//texture=
						break;
					case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:
						//texture =
						break;
					case nrd::ResourceType::TRANSIENT_POOL:
						texture = transientTextures[resource.indexInPool];
						break;
					case nrd::ResourceType::PERMANENT_POOL:
						texture = m_NRDPermanentTextures[resource.indexInPool];
						break;
					default:
						assert(!"Unhandled NRD resource type");
						break;
					}

					assert(texture);

					nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources;
					subresources.baseMipLevel = 0;
					subresources.numMipLevels = 1;

					nvrhi::BindingSetItem setItem = nvrhi::BindingSetItem::None();
					setItem.resourceHandle = texture;
					setItem.slot = nrdDescriptorRange.baseRegisterIndex + descriptorOffset;
					setItem.subresources = subresources;
					setItem.type = (nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE)
						? nvrhi::ResourceType::Texture_SRV
						: nvrhi::ResourceType::Texture_UAV;

					bindingSetDesc.bindings.push_back(setItem);

					resourceIndex++;
				}
			}
			assert(resourceIndex == dispatchDesc.resourcesNum);

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = nrdPipelineDesc.shaderFileName;
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = { dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1 };

            g_Graphic.AddComputePass(computePassParams);
		}
	}

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
        TraceShadows(commandList, renderGraph);
		//DenoiseShadows(commandList, renderGraph);
	}
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
