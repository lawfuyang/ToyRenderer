#include "Graphic.h"

#include "CommonResources.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/PostProcessStructs.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_BloomRDGTextureHandle;

class PostProcessRenderer : public IRenderer
{
public:
    PostProcessRenderer() : IRenderer("PostProcessRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
	{
        const auto& bloomControllables = g_GraphicPropertyGrid.m_BloomControllables;
        if (!bloomControllables.m_bEnabled)
        {
            return false;
        }

        renderGraph.AddReadDependency(g_BloomRDGTextureHandle);
        renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);

		return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();

        const auto& bloomControllables = g_GraphicPropertyGrid.m_BloomControllables;

        // Render Targets & Depth Buffer
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());
        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

        PostProcessParameters passParameters{};
        passParameters.m_OutputDims = g_Graphic.m_RenderResolution;
        passParameters.m_ManualExposure = g_GraphicPropertyGrid.m_AdaptLuminanceControllables.m_ManualExposureOverride;
        passParameters.m_MiddleGray = g_GraphicPropertyGrid.m_AdaptLuminanceControllables.m_MiddleGray;
        passParameters.m_WhitePoint = g_GraphicPropertyGrid.m_AdaptLuminanceControllables.m_WhitePoint;
        passParameters.m_BloomStrength = bloomControllables.m_bEnabled ? bloomControllables.m_BloomStrength : 0.0f;

        nvrhi::TextureHandle lightingOutput = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
        nvrhi::TextureHandle bloomTexture = renderGraph.GetTexture(g_BloomRDGTextureHandle);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
            nvrhi::BindingSetItem::Texture_SRV(0, lightingOutput),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, scene->m_LuminanceBuffer),
            nvrhi::BindingSetItem::Texture_SRV(2, bloomTexture),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
        };

        g_Graphic.AddFullScreenPass(
            commandList,
            frameBufferDesc,
            bindingSetDesc,
            "postprocess_PS_PostProcess",
            nullptr, // no blend state
            nullptr, // no depth stencil state
            nullptr, // default viewport
            &passParameters,
            sizeof(passParameters));
    }
};

static PostProcessRenderer gs_PostProcessRenderer;
IRenderer* g_PostProcessRenderer = &gs_PostProcessRenderer;
