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

RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;

class ShadowMaskRenderer : public IRenderer
{
	nrd::Instance* m_NRDInstance = nullptr;
	nvrhi::BufferHandle m_NRDConstantBuffer;
    nvrhi::SamplerHandle m_Samplers[(uint32_t)nrd::Sampler::MAX_NUM];

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

        const nrd::DenoiserDesc denoiserDescs[] = { 0, nrd::Denoiser::SIGMA_SHADOW };

        nrd::InstanceCreationDesc instanceCreationDesc{};
		instanceCreationDesc.denoisers = denoiserDescs;
		instanceCreationDesc.denoisersNum = std::size(denoiserDescs);

		NRD_CALL(nrd::CreateInstance(instanceCreationDesc, m_NRDInstance));

		const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_NRDInstance);

		const nvrhi::BufferDesc constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(instanceDesc.constantBufferMaxDataSize, "NrdConstantBuffer", 1);
		m_NRDConstantBuffer = device->createBuffer(constantBufferDesc);

		assert(instanceDesc.samplersNum == std::size(m_Samplers));
		for (uint32_t i = 0; i < std::size(m_Samplers); ++i)
		{
			const nrd::Sampler& samplerMode = instanceDesc.samplers[i];

			switch (samplerMode)
			{
			case nrd::Sampler::NEAREST_CLAMP:
                m_Samplers[i] = g_CommonResources.PointClampSampler;
				break;
			case nrd::Sampler::LINEAR_CLAMP:
                m_Samplers[i] = g_CommonResources.LinearClampSampler;
				break;
			default:
				assert(!"Unknown NRD sampler mode");
				break;
			}
		}
    }

	bool Setup(RenderGraph& renderGraph) override
    {
		if (!g_Scene->IsShadowsEnabled())
		{
			return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = nvrhi::Format::R8_UNORM;
		desc.debugName = "Shadow Mask Texture";
		desc.isRenderTarget = true;
		desc.isUAV = true;
		desc.setClearValue(nvrhi::Color{ 1.0f });
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		renderGraph.CreateTransientResource(g_ShadowMaskRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);

        return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		View& view = g_Scene->m_View;

		const auto& controllables = g_GraphicPropertyGrid.m_ShadowControllables;

		nvrhi::TextureHandle shadowMaskTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

		const float sunSize = tanf(0.5f * ConvertToRadians(controllables.m_SunSolidAngle));

		ShadowMaskConsts passConstants;
		passConstants.m_ClipToWorld = view.m_ClipToWorld;
		passConstants.m_DirectionalLightDirection = g_Scene->m_DirLightVec;
		passConstants.m_OutputResolution = Vector2U{ shadowMaskTexture->getDesc().width , shadowMaskTexture->getDesc().height };
		passConstants.m_NoisePhase = (g_Graphic.m_FrameCounter & 0xff) * kGoldenRatio;
		passConstants.m_SunSize = controllables.m_bEnableSoftShadows ? sunSize : 0.0f;
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

        nrd::SigmaSettings sigmaSettings{};
        memcpy(sigmaSettings.lightDirection, &g_Scene->m_DirLightVec, sizeof(sigmaSettings.lightDirection));

		nrd::SetDenoiserSettings(*m_NRDInstance, 0, &sigmaSettings);
	}
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
