#include "Graphic.h"

#include "CommonResources.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "TileRenderingHelper.h"

#include "shaders/shared/DeferredLightingStructs.h"

RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
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
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.setClearValue(nvrhi::Color{ 0.0f });

		renderGraph.CreateTransientResource(g_LightingOutputRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
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
		View& view = scene->m_View;

		const auto& lightingControllables = g_GraphicPropertyGrid.m_LightingControllables;
		const auto& AOControllables = g_GraphicPropertyGrid.m_AmbientOcclusionControllables;
		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

		// pass constants
		DeferredLightingConsts passConstants;
		passConstants.m_CameraOrigin = view.m_Eye;
		passConstants.m_DirectionalLightColor = scene->m_DirLightColor;
		passConstants.m_DirectionalLightStrength = scene->m_DirLightStrength;
		passConstants.m_DirectionalLightVector = scene->m_DirLightVec;
		passConstants.m_InvViewProjMatrix = view.m_InvViewProjectionMatrix;
		passConstants.m_SSAOEnabled = AOControllables.m_bEnabled;
		passConstants.m_LightingOutputResolution = g_Graphic.m_RenderResolution;
		passConstants.m_DebugMode = lightingControllables.m_DebugMode;
		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
		nvrhi::TextureHandle ssaoTexture = AOControllables.m_bEnabled ? renderGraph.GetTexture(g_SSAORDGTextureHandle) : g_CommonResources.R8UIntMax2DTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle shadowMaskTexture = shadowControllables.m_bEnabled ? renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle) : g_CommonResources.WhiteTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, GBufferATexture),
			nvrhi::BindingSetItem::Texture_SRV(1, depthBufferCopyTexture),
			nvrhi::BindingSetItem::Texture_SRV(2, ssaoTexture),
			nvrhi::BindingSetItem::Texture_SRV(3, shadowMaskTexture),
			nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
		};

		nvrhi::FramebufferDesc frameBufferDesc;
		frameBufferDesc.addColorAttachment(lightingOutputTexture);
		frameBufferDesc.setDepthAttachment(depthStencilBuffer)
			.depthAttachment.isReadOnly = true;

		nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthNoneStencilRead;
		depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
		depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Equal;

		const bool bHasDebugView = lightingControllables.m_DebugMode != 0;
		const char* shaderName = bHasDebugView ? "deferredlighting_PS_Main_Debug" : "deferredlighting_PS_Main";

		g_Graphic.AddFullScreenPass(commandList, frameBufferDesc, bindingSetDesc, shaderName, nullptr, &depthStencilState);
	}
};

static DeferredLightingRenderer gs_DeferredLightingRenderer;
IRenderer* g_DeferredLightingRenderer = &gs_DeferredLightingRenderer;
