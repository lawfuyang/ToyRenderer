#include "Graphic.h"

#include "CommonResources.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
extern RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_SSAORDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_RTDDRTDDGIVolumeDescsBuffer;

class DeferredLightingRenderer : public IRenderer
{
public:
	DeferredLightingRenderer() : IRenderer("DeferredLightingRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
    {
		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = GraphicConstants::kLightingOutputFormat;
		desc.debugName = "Lighting Output";
		desc.isRenderTarget = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.setClearValue(nvrhi::Color{ 0.0f });

		renderGraph.CreateTransientResource(g_LightingOutputRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferMotionRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

		if (g_Scene->m_bEnableAO)
		{
			renderGraph.AddReadDependency(g_SSAORDGTextureHandle);
		}

		if (g_Scene->IsShadowsEnabled())
		{
			renderGraph.AddReadDependency(g_ShadowMaskRDGTextureHandle);
		}

		if (g_Scene->IsRTDDGIEnabled())
		{
			renderGraph.AddReadDependency(g_RTDDRTDDGIVolumeDescsBuffer);
		}

		assert(g_Scene->m_RTDDGIVolume);

		return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

		// pass constants
		DeferredLightingConsts passConstants;
		passConstants.m_CameraOrigin = g_Scene->m_View.m_Eye;
		passConstants.m_DirectionalLightVector = g_Scene->m_DirLightVec;
        passConstants.m_DirectionalLightStrength = g_Scene->m_DirLightStrength;
		passConstants.m_ClipToWorld = g_Scene->m_View.m_ClipToWorld;
		passConstants.m_SSAOEnabled = g_Scene->m_bEnableAO;
		passConstants.m_LightingOutputResolution = g_Graphic.m_RenderResolution;
		passConstants.m_DebugMode = g_Scene->m_DebugViewMode;
        passConstants.m_bRTDDGIEnabled = g_Scene->IsRTDDGIEnabled();
		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle GBufferMotionTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
		nvrhi::TextureHandle ssaoTexture = g_Scene->m_bEnableAO ? renderGraph.GetTexture(g_SSAORDGTextureHandle) : g_CommonResources.R8UIntMax2DTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle shadowMaskTexture = g_Scene->IsShadowsEnabled() ? renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle) : g_CommonResources.WhiteTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        nvrhi::BufferHandle RTDDGIVolumeDescsBuffer = g_Scene->IsRTDDGIEnabled() ? renderGraph.GetBuffer(g_RTDDRTDDGIVolumeDescsBuffer) : g_CommonResources.DummyUIntStructuredBuffer;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, GBufferATexture),
            nvrhi::BindingSetItem::Texture_SRV(1, GBufferMotionTexture),
			nvrhi::BindingSetItem::Texture_SRV(2, depthBufferCopyTexture),
			nvrhi::BindingSetItem::Texture_SRV(3, ssaoTexture),
			nvrhi::BindingSetItem::Texture_SRV(4, shadowMaskTexture),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, RTDDGIVolumeDescsBuffer),
			nvrhi::BindingSetItem::Texture_SRV(6, g_Scene->m_RTDDGIVolume->GetProbeDataTexture()),
			nvrhi::BindingSetItem::Texture_SRV(7, g_Scene->m_RTDDGIVolume->GetProbeIrradianceTexture()),
			nvrhi::BindingSetItem::Texture_SRV(8, g_Scene->m_RTDDGIVolume->GetProbeDistanceTexture()),
			nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler),
			nvrhi::BindingSetItem::Sampler(1, g_CommonResources.LinearWrapSampler),
		};

		nvrhi::FramebufferDesc frameBufferDesc;
		frameBufferDesc.addColorAttachment(lightingOutputTexture);
		frameBufferDesc.setDepthAttachment(depthStencilBuffer)
			.depthAttachment.isReadOnly = true;

		nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthNoneStencilRead;
		depthStencilState.stencilRefValue = GraphicConstants::kStencilBit_Opaque;
		depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Equal;

		const bool bHasDebugView = g_Scene->m_DebugViewMode != 0;
		const char* shaderName = bHasDebugView ? "deferredlighting_PS_Main_Debug" : "deferredlighting_PS_Main";

		Graphic::FullScreenPassParams fullScreenPassParams;
		fullScreenPassParams.m_CommandList = commandList;
		fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
		fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
		fullScreenPassParams.m_ShaderName = shaderName;
		fullScreenPassParams.m_DepthStencilState = &depthStencilState;

		g_Graphic.AddFullScreenPass(fullScreenPassParams);
	}
};

static DeferredLightingRenderer gs_DeferredLightingRenderer;
IRenderer* g_DeferredLightingRenderer = &gs_DeferredLightingRenderer;
