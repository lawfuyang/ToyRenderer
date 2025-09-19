#include "Graphic.h"

#include "CommonResources.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/ShaderInterop.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_UpscaledLightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_BloomRDGTextureHandle;

class PostProcessRenderer : public IRenderer
{
public:
    PostProcessRenderer() : IRenderer("PostProcessRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
	{
        if (g_Scene->m_bEnableBloom)
        {
            renderGraph.AddReadDependency(g_BloomRDGTextureHandle);
        }

        if (g_Scene->m_bEnableTAA)
        {
            renderGraph.AddReadDependency(g_UpscaledLightingOutputRDGTextureHandle);
        }
        else
        {
            renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);
        }

		return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        // Render Targets & Depth Buffer
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());
        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

        PostProcessParameters passParameters{};
        passParameters.m_OutputDims = g_Graphic.m_RenderResolution;
        passParameters.m_ManualExposure = g_Scene->m_ManualExposureOverride;
        passParameters.m_MiddleGray = g_Scene->m_MiddleGray;
        passParameters.m_BloomStrength = g_Scene->m_bEnableBloom ? g_Scene->m_BloomStrength : 0.0f;

        nvrhi::TextureHandle inputTexture = renderGraph.GetTexture(g_Scene->m_bEnableTAA ? g_UpscaledLightingOutputRDGTextureHandle : g_LightingOutputRDGTextureHandle);
        nvrhi::TextureHandle bloomTexture = g_Scene->m_bEnableBloom ? renderGraph.GetTexture(g_BloomRDGTextureHandle) : g_CommonResources.BlackTexture.m_NVRHITextureHandle;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
            nvrhi::BindingSetItem::Texture_SRV(0, inputTexture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Scene->m_LuminanceBuffer),
            nvrhi::BindingSetItem::Texture_SRV(2, bloomTexture),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
        };

        Graphic::FullScreenPassParams fullScreenPassParams;
        fullScreenPassParams.m_CommandList = commandList;
        fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
        fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
        fullScreenPassParams.m_ShaderName = "postprocess_PS_PostProcess";
        fullScreenPassParams.m_PushConstantsData = &passParameters;
        fullScreenPassParams.m_PushConstantsBytes = sizeof(passParameters);

        g_Graphic.AddFullScreenPass(fullScreenPassParams);
    }
};

static PostProcessRenderer gs_PostProcessRenderer;
IRenderer* g_PostProcessRenderer = &gs_PostProcessRenderer;
