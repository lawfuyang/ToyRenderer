#include "Graphic.h"

#include "Scene.h"
#include "GraphicPropertyGrid.h"
#include "CommonResources.h"
#include "RenderGraph.h"

#include "shaders/ShaderInterop.h"

RenderGraph::ResourceHandle g_BloomRDGTextureHandle;
extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class BloomRenderer : public IRenderer
{
public:
	BloomRenderer() : IRenderer("BloomRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
	{
		const GraphicPropertyGrid::BloomControllables& bloomControllables = g_GraphicPropertyGrid.m_BloomControllables;
		if (!bloomControllables.m_bEnabled)
		{
			return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = Graphic::kLightingOutputFormat;
		desc.debugName = "Bloom Texture";
		desc.mipLevels = bloomControllables.m_NbBloomMips;
		desc.isRenderTarget = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;

		renderGraph.CreateTransientResource(g_BloomRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);

		return true;
	}

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		const GraphicPropertyGrid::BloomControllables& bloomControllables = g_GraphicPropertyGrid.m_BloomControllables;
		if (!bloomControllables.m_bEnabled)
		{
			return;
		}

		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();

		const uint32_t nbPasses = bloomControllables.m_NbBloomMips - 1;

		nvrhi::TextureHandle lightingOutput = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
		nvrhi::TextureHandle bloomTexture = renderGraph.GetTexture(g_BloomRDGTextureHandle);

		const nvrhi::TextureDesc& textureDesc = lightingOutput->getDesc();

		// downsample
		for (uint32_t i = 0; i < nbPasses; ++i)
		{
			const bool bIsFirstPass = (i == 0);
			const uint32_t srcMip = i;
			const uint32_t destMip = srcMip + 1;
			nvrhi::TextureHandle srcTexture = bIsFirstPass ? lightingOutput : bloomTexture;

			const Vector2U srcRes{ textureDesc.width >> srcMip, textureDesc.height >> srcMip };
			const Vector2U destRes{ textureDesc.width >> destMip, textureDesc.height >> destMip };

			BloomDownsampleConsts downsampleConsts;
			downsampleConsts.m_bIsFirstDownsample = bIsFirstPass;
			downsampleConsts.m_InvSourceResolution = Vector2{ 1.0f / srcRes.x, 1.0f / srcRes.y };

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(downsampleConsts)),
				nvrhi::BindingSetItem::Texture_SRV(0, srcTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ srcMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
				nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
			};

			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment(bloomTexture, nvrhi::TextureSubresourceSet{ destMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices });

			const nvrhi::Viewport viewPort = nvrhi::Viewport{ (float)destRes.x, (float)destRes.y };

			g_Graphic.AddFullScreenPass(
				commandList,
				framebufferDesc,
				bindingSetDesc,
				"bloom_PS_Downsample",
				nullptr, // no blend state
				nullptr, // no depth stencil state
				&viewPort,
				&downsampleConsts,
				sizeof(downsampleConsts));
		}

		// upsample
		for (uint32_t i = 0; i < nbPasses; ++i)
		{
			const uint32_t srcMip = nbPasses - i;
			const uint32_t destMip = srcMip - 1;

			BloomUpsampleConsts upsampleConsts;
			upsampleConsts.m_FilterRadius = bloomControllables.m_UpsampleFilterRadius;

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(upsampleConsts)),
				nvrhi::BindingSetItem::Texture_SRV(0, bloomTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ srcMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
				nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
			};

			const Vector2U destRes{ textureDesc.width >> destMip, textureDesc.height >> destMip };

			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment(bloomTexture, nvrhi::TextureSubresourceSet{ destMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices });

			const nvrhi::Viewport viewPort = nvrhi::Viewport{ (float)destRes.x, (float)destRes.y };

			g_Graphic.AddFullScreenPass(
				commandList,
				framebufferDesc,
				bindingSetDesc,
				"bloom_PS_Upsample",
				nullptr, // no blend state
				nullptr, // no depth stencil state
				&viewPort,
				&upsampleConsts,
				sizeof(upsampleConsts));
		}
	}
};

static BloomRenderer gs_BloomRenderer;
IRenderer* g_BloomRenderer = &gs_BloomRenderer;
