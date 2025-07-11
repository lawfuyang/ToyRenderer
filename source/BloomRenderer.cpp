#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "Scene.h"
#include "CommonResources.h"
#include "RenderGraph.h"

#include "shaders/ShaderInterop.h"

RenderGraph::ResourceHandle g_BloomRDGTextureHandle;
extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class BloomRenderer : public IRenderer
{
	uint32_t m_NbBloomMips = 6;
	float m_UpsampleFilterRadius = 0.005f;

public:
	BloomRenderer() : IRenderer("BloomRenderer") {}

	void UpdateImgui() override
	{
		const uint32_t nbMaxBloomMips = ComputeNbMips(g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y);

		ImGui::Checkbox("Enabled", &g_Scene->m_bEnableBloom);
		ImGui::SliderInt("Number of Bloom Mips", (int*)&m_NbBloomMips, 2, nbMaxBloomMips);
		ImGui::SliderFloat("Upsample Filter Radius", &m_UpsampleFilterRadius, 0.001f, 0.1f);
		ImGui::SliderFloat("Bloom Strength", &g_Scene->m_BloomStrength, 0.01f, 1.0f);
	}

	bool Setup(RenderGraph& renderGraph) override
	{
		if (!g_Scene->m_bEnableBloom)
		{
			return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = GraphicConstants::kLightingOutputFormat;
		desc.debugName = "Bloom Texture";
		desc.mipLevels = m_NbBloomMips;
		desc.isRenderTarget = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;

		renderGraph.CreateTransientResource(g_BloomRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);

		return true;
	}

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

		const uint32_t nbPasses = m_NbBloomMips - 1;

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

			BloomConsts bloomConsts;
			bloomConsts.m_bIsFirstDownsample = bIsFirstPass;
			bloomConsts.m_InvSourceResolution = Vector2{ 1.0f / srcRes.x, 1.0f / srcRes.y };

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(bloomConsts)),
				nvrhi::BindingSetItem::Texture_SRV(0, srcTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ srcMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
				nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
			};

			nvrhi::FramebufferDesc frameBufferDesc;
			frameBufferDesc.addColorAttachment(bloomTexture, nvrhi::TextureSubresourceSet{ destMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices });

			const nvrhi::Viewport viewPort = nvrhi::Viewport{ (float)destRes.x, (float)destRes.y };

			Graphic::FullScreenPassParams fullScreenPassParams;
			fullScreenPassParams.m_CommandList = commandList;
			fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
			fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
			fullScreenPassParams.m_ShaderName = "bloom_PS_Downsample";
            fullScreenPassParams.m_ViewPort = &viewPort;
			fullScreenPassParams.m_PushConstantsData = &bloomConsts;
			fullScreenPassParams.m_PushConstantsBytes = sizeof(bloomConsts);

            g_Graphic.AddFullScreenPass(fullScreenPassParams);
		}

		// upsample
		for (uint32_t i = 0; i < nbPasses; ++i)
		{
			const uint32_t srcMip = nbPasses - i;
			const uint32_t destMip = srcMip - 1;

			BloomConsts bloomConsts;
			bloomConsts.m_FilterRadius = m_UpsampleFilterRadius;

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::PushConstants(0, sizeof(bloomConsts)),
				nvrhi::BindingSetItem::Texture_SRV(0, bloomTexture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ srcMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
				nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
			};

			const Vector2U destRes{ textureDesc.width >> destMip, textureDesc.height >> destMip };

			nvrhi::FramebufferDesc frameBufferDesc;
			frameBufferDesc.addColorAttachment(bloomTexture, nvrhi::TextureSubresourceSet{ destMip, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices });

			const nvrhi::Viewport viewPort = nvrhi::Viewport{ (float)destRes.x, (float)destRes.y };

			Graphic::FullScreenPassParams fullScreenPassParams;
			fullScreenPassParams.m_CommandList = commandList;
			fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
			fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
			fullScreenPassParams.m_ShaderName = "bloom_PS_Upsample";
			fullScreenPassParams.m_ViewPort = &viewPort;
			fullScreenPassParams.m_PushConstantsData = &bloomConsts;
			fullScreenPassParams.m_PushConstantsBytes = sizeof(bloomConsts);

			g_Graphic.AddFullScreenPass(fullScreenPassParams);
		}
	}
};

static BloomRenderer gs_BloomRenderer;
IRenderer* g_BloomRenderer = &gs_BloomRenderer;
