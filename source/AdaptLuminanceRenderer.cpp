#include "Graphic.h"

#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/shared/AdaptLuminanceStructs.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class AdaptLuminanceRenderer : public IRenderer
{
    FencedReadbackBuffer m_ExposureReadbackBuffer;
    RenderGraph::ResourceHandle m_LuminanceHistogramRDGBufferHandle;

public:
    AdaptLuminanceRenderer() : IRenderer("AdaptLuminanceRenderer") {}

    void Initialize() override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        m_ExposureReadbackBuffer.Initialize(device, sizeof(float));
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        const GraphicPropertyGrid::AdaptLuminanceControllables& controllables = g_GraphicPropertyGrid.m_AdaptLuminanceControllables;

        if (controllables.m_ManualExposureOverride > 0.0f)
        {
            return false;
        }

        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(uint32_t) * 256;
        desc.structStride = sizeof(uint32_t);
        desc.debugName = "Luminance Histogram";
        desc.canHaveUAVs = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        renderGraph.CreateTransientResource(m_LuminanceHistogramRDGBufferHandle, desc);

        renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();

        // read back previous frame's scene exposure
        m_ExposureReadbackBuffer.Read(device, (void*)&scene->m_LastFrameExposure);

        const GraphicPropertyGrid::AdaptLuminanceControllables& controllables = g_GraphicPropertyGrid.m_AdaptLuminanceControllables;

        ON_EXIT_SCOPE_LAMBDA([&]
            {
                // copy to staging texture to be read back by CPU next frame, regardless whether manual exposure mode is enabled or not
                m_ExposureReadbackBuffer.CopyTo(device, commandList, scene->m_LuminanceBuffer);
            });

        if (controllables.m_ManualExposureOverride > 0.0f)
        {
            commandList->writeBuffer(scene->m_LuminanceBuffer, &controllables.m_ManualExposureOverride, sizeof(float));
            return;
        }

        const float minLogLum = std::log2(controllables.m_MinimumLuminance);
        const float maxLogLum = std::log2(controllables.m_MaximumLuminance);

        nvrhi::TextureHandle lightingOutput = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
        nvrhi::BufferHandle luminanceHistogramBuffer = renderGraph.GetBuffer(m_LuminanceHistogramRDGBufferHandle);

        // GenerateLuminanceHistogram
        {
            commandList->clearBufferUInt(luminanceHistogramBuffer, 0);

            GenerateLuminanceHistogramParameters passParameters{};
            passParameters.m_SrcColorDims = g_Graphic.m_RenderResolution;
            passParameters.m_MinLogLuminance = minLogLum;
            passParameters.m_InverseLogLuminanceRange = 1.0f / (maxLogLum - minLogLum);

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                nvrhi::BindingSetItem::Texture_SRV(0, lightingOutput),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, luminanceHistogramBuffer)
            };

            const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(passParameters.m_SrcColorDims, Vector2U{ 16, 16 });
            g_Graphic.AddComputePass(commandList, "adaptluminance_CS_GenerateLuminanceHistogram", bindingSetDesc, dispatchGroupSize, &passParameters, sizeof(passParameters));
        }

        // AdaptExposure
        {
            AdaptExposureParameters passParameters{};
            passParameters.m_AdaptationSpeed = Saturate(controllables.m_AutoExposureSpeed * g_Engine.m_CPUCappedFrameTimeMs);
            passParameters.m_MinLogLuminance = minLogLum;
            passParameters.m_LogLuminanceRange = maxLogLum - minLogLum;
            passParameters.m_NbPixels = g_Graphic.m_RenderResolution.x * g_Graphic.m_RenderResolution.y;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, luminanceHistogramBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, scene->m_LuminanceBuffer)
            };

            const Vector3U dispatchGroupSize{1, 1, 1};
            g_Graphic.AddComputePass(commandList, "adaptluminance_CS_AdaptExposure", bindingSetDesc, dispatchGroupSize, &passParameters, sizeof(passParameters));
        }
    }
};

static AdaptLuminanceRenderer gs_AdaptLuminanceRenderer;
IRenderer* g_AdaptLuminanceRenderer = &gs_AdaptLuminanceRenderer;
