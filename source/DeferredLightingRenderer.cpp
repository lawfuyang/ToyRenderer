#include "Graphic.h"

#include "CommonResources.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "TileRenderingHelper.h"

#include "shaders/shared/DeferredLightingStructs.h"

RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferBRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferCRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferDRDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_ConservativeShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_SSAORDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class DeferredLightingRenderer : public IRenderer
{
public:
	DeferredLightingRenderer() : IRenderer("DeferredLightingRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
    {
		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = Graphic::kLightingOutputFormat;
		desc.debugName = "Lighting Output";
		desc.isRenderTarget = true;
		desc.isUAV = g_GraphicPropertyGrid.m_LightingControllables.m_bDeferredLightingUseCS;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.setClearValue(nvrhi::Color{ 0.0f });

		renderGraph.CreateTransientResource(g_LightingOutputRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
		renderGraph.AddReadDependency(g_GBufferBRDGTextureHandle);
		renderGraph.AddReadDependency(g_GBufferCRDGTextureHandle);
		renderGraph.AddReadDependency(g_GBufferDRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

		const GraphicPropertyGrid::AmbientOcclusionControllables& AOControllables = g_GraphicPropertyGrid.m_AmbientOcclusionControllables;
		if (AOControllables.m_bEnabled)
		{
			renderGraph.AddReadDependency(g_SSAORDGTextureHandle);
		}

		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
		if (shadowControllables.m_bEnabled)
		{
			renderGraph.AddReadDependency(g_ShadowMaskRDGTextureHandle);
		}

		return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();
		View& view = scene->m_Views[Scene::EView::Main];

		const auto& lightingControllables = g_GraphicPropertyGrid.m_LightingControllables;
		const auto& AOControllables = g_GraphicPropertyGrid.m_AmbientOcclusionControllables;
		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

		// pass constants
		DeferredLightingConsts passConstants;
		passConstants.m_CameraOrigin = view.m_Eye;
		passConstants.m_DirectionalLightColor = scene->m_DirLightColor;
		passConstants.m_DirectionalLightStrength = scene->m_DirLightStrength;
		passConstants.m_DirectionalLightVector = scene->m_DirLightVec;
		passConstants.m_InvShadowMapResolution = 1.0f / g_GraphicPropertyGrid.m_ShadowControllables.m_ShadowMapResolution;
		passConstants.m_InvViewProjMatrix = view.m_InvViewProjectionMatrix;
		passConstants.m_SSAOEnabled = AOControllables.m_bEnabled;
		passConstants.m_LightingOutputResolution = g_Graphic.m_RenderResolution;

		passConstants.m_DebugFlags = 0;
		passConstants.m_DebugFlags |= lightingControllables.m_bLightingOnlyDebug ? kDeferredLightingDebugFlag_LightingOnly : 0;
		passConstants.m_DebugFlags |= g_GraphicPropertyGrid.m_InstanceRenderingControllables.m_bColorizeInstances ? kDeferredLightingDebugFlag_ColorizeInstances : 0;

		for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
		{
			passConstants.m_DirLightViewProj[i] = scene->m_Views[Scene::EView::CSM0 + i].m_ViewProjectionMatrix;
		}

		memcpy(&passConstants.m_CSMDistances, scene->m_CSMSplitDistances, sizeof(passConstants.m_CSMDistances));

		nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
		nvrhi::TextureHandle GBufferBTexture = renderGraph.GetTexture(g_GBufferBRDGTextureHandle);
		nvrhi::TextureHandle GBufferCTexture = renderGraph.GetTexture(g_GBufferCRDGTextureHandle);
		nvrhi::TextureHandle GBufferDTexture = renderGraph.GetTexture(g_GBufferDRDGTextureHandle);
		nvrhi::TextureHandle ssaoTexture = AOControllables.m_bEnabled ? renderGraph.GetTexture(g_SSAORDGTextureHandle) : g_CommonResources.R8UIntMax2DTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle shadowMaskTexture = shadowControllables.m_bEnabled ? renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle) : g_CommonResources.WhiteTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		const bool bHasDebugView = (passConstants.m_DebugFlags != 0);

		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, GBufferATexture),
			nvrhi::BindingSetItem::Texture_SRV(1, GBufferBTexture),
			nvrhi::BindingSetItem::Texture_SRV(2, GBufferCTexture),
			nvrhi::BindingSetItem::Texture_SRV(3, GBufferDTexture),
			nvrhi::BindingSetItem::Texture_SRV(4, depthBufferCopyTexture),
			nvrhi::BindingSetItem::Texture_SRV(5, ssaoTexture),
			nvrhi::BindingSetItem::Texture_SRV(6, shadowMaskTexture),
			nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
		};

		if (lightingControllables.m_bDeferredLightingUseCS)
		{
			bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(0, lightingOutputTexture));

			const char* shaderName = bHasDebugView ? "deferredlighting_CS_Main_Debug" : "deferredlighting_CS_Main";

			g_Graphic.AddComputePass(
				commandList,
				shaderName,
				bindingSetDesc,
				ComputeShaderUtils::GetGroupCount(g_Graphic.m_RenderResolution, Vector2U{ 8, 8 }));
		}
		else
		{
			nvrhi::FramebufferDesc frameBufferDesc;
			frameBufferDesc.addColorAttachment(lightingOutputTexture);
			frameBufferDesc.setDepthAttachment(depthStencilBuffer)
				.depthAttachment.isReadOnly = true;

			nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthNoneStencilRead;
			depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
			depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Equal;

			const char* shaderName = bHasDebugView ? "deferredlighting_PS_Main_Debug" : "deferredlighting_PS_Main";

			g_Graphic.AddFullScreenPass(commandList, frameBufferDesc, bindingSetDesc, shaderName, nullptr, &depthStencilState);
		}
	}
};

static DeferredLightingRenderer gs_DeferredLightingRenderer;
IRenderer* g_DeferredLightingRenderer = &gs_DeferredLightingRenderer;
