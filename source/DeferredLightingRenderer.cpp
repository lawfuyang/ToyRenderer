#include "Graphic.h"

#include "CommonResources.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/DeferredLightingStructs.h"

static_assert(kDeferredLightingTileSize == TileRenderingHelper::kTileSize, "Tile size mismatch");

RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferBRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferCRDGTextureHandle;
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
		desc.isUAV = g_GraphicPropertyGrid.m_LightingControllables.m_bTileRenderingUseCS;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.setClearValue(nvrhi::Color{ 0.0f });

		renderGraph.CreateTransientResource(g_LightingOutputRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
		renderGraph.AddReadDependency(g_GBufferBRDGTextureHandle);
		renderGraph.AddReadDependency(g_GBufferCRDGTextureHandle);
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

		g_Graphic.m_Scene->m_DeferredLightingTileRenderingHelper.AddReadDependencies(renderGraph);

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
		const TileRenderingHelper& tileRenderingHelper = scene->m_DeferredLightingTileRenderingHelper;

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
		passConstants.m_NbTiles = Vector2U{ tileRenderingHelper.m_GroupCount.x, tileRenderingHelper.m_GroupCount.y };

		passConstants.m_DebugFlags = 0;
		passConstants.m_DebugFlags |= lightingControllables.m_bLightingOnlyDebug ? kDeferredLightingDebugFlag_LightingOnly : 0;
		passConstants.m_DebugFlags |= g_GraphicPropertyGrid.m_DebugControllables.m_bColorizeInstances ? kDeferredLightingDebugFlag_ColorizeInstances : 0;

		for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
		{
			passConstants.m_DirLightViewProj[i] = scene->m_Views[Scene::EView::CSM0 + i].m_ViewProjectionMatrix;
		}

		memcpy(&passConstants.m_CSMDistances, scene->m_CSMSplitDistances, sizeof(passConstants.m_CSMDistances));

		nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
		nvrhi::TextureHandle GBufferBTexture = renderGraph.GetTexture(g_GBufferBRDGTextureHandle);
		nvrhi::TextureHandle GBufferCTexture = renderGraph.GetTexture(g_GBufferCRDGTextureHandle);
		nvrhi::TextureHandle ssaoTexture = AOControllables.m_bEnabled ? renderGraph.GetTexture(g_SSAORDGTextureHandle) : g_CommonResources.R8UIntMax2DTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle shadowMaskTexture = shadowControllables.m_bEnabled ? renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle) : g_CommonResources.WhiteTexture.m_NVRHITextureHandle;
		nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		const bool bHasDebugView = (passConstants.m_DebugFlags != 0);

		for (uint32_t tileID = Tile_ID_Normal; tileID < Tile_ID_Count; tileID++)
		{
			passConstants.m_TileID = tileID;

			nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
				nvrhi::BindingSetItem::Texture_SRV(0, GBufferATexture),
				nvrhi::BindingSetItem::Texture_SRV(1, GBufferBTexture),
				nvrhi::BindingSetItem::Texture_SRV(2, GBufferCTexture),
				nvrhi::BindingSetItem::Texture_SRV(3, depthBufferCopyTexture),
				nvrhi::BindingSetItem::Texture_SRV(4, ssaoTexture),
				nvrhi::BindingSetItem::Texture_SRV(5, shadowMaskTexture),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
			};

			// NOTE: i'm lazy to support debug Shader for CS codepath...
			if (lightingControllables.m_bTileRenderingUseCS && !bHasDebugView)
			{
				bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(0, lightingOutputTexture));

				tileRenderingHelper.DispatchTiles(commandList, renderGraph, "deferredlighting_CS_Main", bindingSetDesc, tileID);
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

				tileRenderingHelper.DrawTiles(commandList, renderGraph, shaderName, bindingSetDesc, frameBufferDesc, tileID, nullptr, &depthStencilState);
			}
		}
	}
};

class TileClassificationRenderer : public IRenderer
{
public:
	TileClassificationRenderer() : IRenderer("TileClassificationRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
	{
		g_Graphic.m_Scene->m_DeferredLightingTileRenderingHelper.CreateTransientResources(renderGraph);

		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
		if (shadowControllables.m_bEnabled)
		{
			renderGraph.AddReadDependency(g_ConservativeShadowMaskRDGTextureHandle);
		}

		return true;
	}

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();
		View& view = scene->m_Views[Scene::EView::Main];

		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

		nvrhi::TextureHandle conservativeShadowMaskTexture = shadowControllables.m_bEnabled ? renderGraph.GetTexture(g_ConservativeShadowMaskRDGTextureHandle) : g_CommonResources.WhiteTexture.m_NVRHITextureHandle;

		const TileRenderingHelper& tileRendereringHelper = scene->m_DeferredLightingTileRenderingHelper;

		tileRendereringHelper.ClearBuffers(commandList, renderGraph);

		nvrhi::BufferHandle tileCounterBuffer = renderGraph.GetBuffer(tileRendereringHelper.m_TileCounterRDGBufferHandle);
		nvrhi::BufferHandle dispatchIndirectArgsBuffer = renderGraph.GetBuffer(tileRendereringHelper.m_DispatchIndirectArgsRDGBufferHandle);
		nvrhi::BufferHandle drawIndirectArgsBuffer = renderGraph.GetBuffer(tileRendereringHelper.m_DrawIndirectArgsRDGBufferHandle);
		nvrhi::BufferHandle tileOffsetsBuffer = renderGraph.GetBuffer(tileRendereringHelper.m_TileOffsetsRDGBufferHandle);

		DeferredLightingTileClassificationConsts passConstants;
		passConstants.m_NbTiles = Vector2U{ tileRendereringHelper.m_GroupCount.x, tileRendereringHelper.m_GroupCount.y };

		// '-1' because the 1st mip of any conservative texture is the half render resolution
		const uint32_t kConservativeTextureMip = std::log2(TileRenderingHelper::kTileSize) - 1;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
			nvrhi::BindingSetItem::Texture_SRV(0, conservativeShadowMaskTexture, nvrhi::Format::UNKNOWN, shadowControllables.m_bEnabled ? nvrhi::TextureSubresourceSet{ kConservativeTextureMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices } : nvrhi::AllSubresources),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, tileCounterBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, dispatchIndirectArgsBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, drawIndirectArgsBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(3, tileOffsetsBuffer),
			nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
		};

		g_Graphic.AddComputePass(
			commandList,
			"deferredlighting_CS_TileClassification",
			bindingSetDesc,
			tileRendereringHelper.m_GroupCount,
			&passConstants,
			sizeof(passConstants));
	}
};

class TileClassificationDebugRenderer : public IRenderer
{
public:
	TileClassificationDebugRenderer() : IRenderer("TileClassificationDebugRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
	{
		const GraphicPropertyGrid::LightingControllables& lightingControllables = g_GraphicPropertyGrid.m_LightingControllables;

		if (!lightingControllables.m_bEnableDeferredLightingTileClassificationDebug)
		{
			return false;
		}

		g_Graphic.m_Scene->m_DeferredLightingTileRenderingHelper.AddReadDependencies(renderGraph);

		renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);
		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

		return true;
	}

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		const GraphicPropertyGrid::LightingControllables& lightingControllables = g_GraphicPropertyGrid.m_LightingControllables;

		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();
		View& view = scene->m_Views[Scene::EView::Main];

		const TileRenderingHelper& tileRendereringHelper = scene->m_DeferredLightingTileRenderingHelper;

		// pass constants
		DeferredLightingTileClassificationDebugConsts passConstants;
		passConstants.m_SceneLuminance = scene->m_LastFrameExposure;
		passConstants.m_NbTiles = tileRendereringHelper.m_NbTiles;

		nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		for (uint32_t tileID = Tile_ID_Normal; tileID < Tile_ID_Count; tileID++)
		{
			static const Vector3 kOverlayColors[Tile_ID_Count] = {
                Vector3{ 1.0f, 0.0f, 0.0f },
                Vector3{ 0.0f, 1.0f, 0.0f }
            };

			passConstants.m_OverlayColor = kOverlayColors[tileID];
			passConstants.m_TileID = tileID;

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants))
			};

			if (lightingControllables.m_bTileRenderingUseCS)
			{
				bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopyTexture));
				bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(0, lightingOutputTexture));

				tileRendereringHelper.DispatchTiles(
					commandList,
					renderGraph,
					"deferredlighting_CS_TileClassificationDebug",
					bindingSetDesc,
					tileID,
					&passConstants,
					sizeof(passConstants));
			}
			else
			{
				nvrhi::FramebufferDesc frameBufferDesc;
				frameBufferDesc.addColorAttachment(lightingOutputTexture);
				frameBufferDesc.setDepthAttachment(depthStencilBuffer)
					.depthAttachment.isReadOnly = true;

				const nvrhi::BlendState::RenderTarget& blendState = g_CommonResources.BlendAdditive;

				nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthNoneStencilRead;
				depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
				depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Equal;

				tileRendereringHelper.DrawTiles(
					commandList,
					renderGraph,
					"deferredlighting_PS_TileClassificationDebug",
					bindingSetDesc,
					frameBufferDesc,
					tileID,
					&blendState,
					&depthStencilState,
					&passConstants,
					sizeof(passConstants));
			}
		}
	}
};

static DeferredLightingRenderer gs_DeferredLightingRenderer;
IRenderer* g_DeferredLightingRenderer = &gs_DeferredLightingRenderer;

static TileClassificationRenderer gs_TileClassificationRenderer;
IRenderer* g_TileClassificationRenderer = &gs_TileClassificationRenderer;

static TileClassificationDebugRenderer gs_TileClassificationDebugRenderer;
IRenderer* g_TileClassificationDebugRenderer = &gs_TileClassificationDebugRenderer;