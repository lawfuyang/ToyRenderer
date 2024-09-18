#include "Graphic.h"

#include "CommonResources.h"
#include "FFXHelpers.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/ShadowMaskStructs.h"
#include "shaders/shared/MinMaxDownsampleStructs.h"

RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
RenderGraph::ResourceHandle g_ConservativeShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMapArrayRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class ShadowMaskRenderer : public IRenderer
{
	FFXHelpers::SPD m_SPDHelper;

public:
	ShadowMaskRenderer() : IRenderer("ShadowMaskRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
    {
		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
		if (!shadowControllables.m_bEnabled)
		{
			return false;
		}

		m_SPDHelper.CreateTransientResources(renderGraph);

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = nvrhi::Format::R8_UNORM;
		desc.debugName = "Shadow Mask Texture";
		desc.isRenderTarget = true;
		desc.setClearValue(nvrhi::Color{ 1.0f });
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		renderGraph.CreateTransientResource(g_ShadowMaskRDGTextureHandle, desc);

		desc.width >>= 1;
		desc.height >>= 1;
		desc.debugName = "Convservative Shadow Mask Texture";
		desc.mipLevels = ComputeNbMips(desc.width, desc.height);
		desc.isUAV = true;
		renderGraph.CreateTransientResource(g_ConservativeShadowMaskRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_ShadowMapArrayRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

        return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();
		View& view = scene->m_Views[Scene::EView::Main];

        // pass constants
        ShadowMaskConsts passConstants;
        passConstants.m_CameraOrigin = view.m_Eye;
        passConstants.m_InvShadowMapResolution = 1.0f / g_GraphicPropertyGrid.m_ShadowControllables.m_ShadowMapResolution;
        passConstants.m_InvViewProjMatrix = view.m_InvViewProjectionMatrix;
        passConstants.m_CSMDistances = { scene->m_CSMSplitDistances[0], scene->m_CSMSplitDistances[1], scene->m_CSMSplitDistances[2], scene->m_CSMSplitDistances[3] };

        for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
        {
            passConstants.m_DirLightViewProj[i] = scene->m_Views[Scene::EView::CSM0 + i].m_ViewProjectionMatrix;
        }

		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::TextureHandle shadowMapArray = renderGraph.GetTexture(g_ShadowMapArrayRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopy),
			nvrhi::BindingSetItem::Texture_SRV(1, shadowMapArray),
			nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler),
			nvrhi::BindingSetItem::Sampler(1, g_CommonResources.PointClampComparisonLessSampler),
			nvrhi::BindingSetItem::Sampler(2, g_CommonResources.LinearClampComparisonLessSampler)
		};

		nvrhi::TextureHandle shadowMaskTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
		nvrhi::TextureHandle conservativeShadowMaskTexture = renderGraph.GetTexture(g_ConservativeShadowMaskRDGTextureHandle);

		nvrhi::FramebufferDesc frameBufferDesc;
		frameBufferDesc.addColorAttachment(shadowMaskTexture);
		frameBufferDesc.setDepthAttachment(depthStencilBuffer)
			.depthAttachment.isReadOnly = true;

		nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthNoneStencilNone;
		depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
		depthStencilState.stencilWriteMask = 0;
		depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Equal;

		g_Graphic.AddFullScreenPass(
			commandList,
			frameBufferDesc,
			bindingSetDesc,
			"shadowmask_PS_Main",
			nullptr, // no blend state
			&depthStencilState);

		// generate conservative shadow mask
		{
			const nvrhi::TextureDesc& outputDesc = conservativeShadowMaskTexture->getDesc();

			MinMaxDownsampleConsts minMaxDownsampleConsts;
			minMaxDownsampleConsts.m_OutputDimensions = Vector2U{ outputDesc.width, outputDesc.height };
			minMaxDownsampleConsts.m_bDownsampleMax = true;

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(MinMaxDownsampleConsts)),
				nvrhi::BindingSetItem::Texture_SRV(0, shadowMaskTexture),
				nvrhi::BindingSetItem::Texture_UAV(0, conservativeShadowMaskTexture),
				nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
			};

			const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector2U{ outputDesc.width, outputDesc.height }, 8);
			g_Graphic.AddComputePass(commandList, "minmaxdownsample_CS_Main", bindingSetDesc, dispatchGroupSize, &minMaxDownsampleConsts, sizeof(minMaxDownsampleConsts));

			m_SPDHelper.Execute(commandList, renderGraph, shadowMaskTexture, conservativeShadowMaskTexture, nvrhi::SamplerReductionType::Maximum);
		}
	}
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
