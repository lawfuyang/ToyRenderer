#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class AdaptLuminanceRenderer : public IRenderer
{
    nvrhi::BufferHandle m_LuminanceReadbackBuffers[2];
    nvrhi::StagingTextureHandle m_ExposureReadbackTextures[2];
    RenderGraph::ResourceHandle m_LuminanceHistogramRDGBufferHandle;

    float m_MinimumLuminance = 0.004f;
    float m_MaximumLuminance = 12.0f;
    float m_AutoExposureSpeed = 0.0025f;

public:
    AdaptLuminanceRenderer() : IRenderer("AdaptLuminanceRenderer") {}

    void Initialize() override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "AdaptLuminanceRenderer Init");

        for (uint32_t i = 0; i < 2; ++i)
        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(float);
            desc.debugName = "Luminance Readback Buffer";
            desc.initialState = nvrhi::ResourceStates::CopyDest;
            desc.cpuAccess = nvrhi::CpuAccessMode::Read;

            m_LuminanceReadbackBuffers[i] = device->createBuffer(desc);
        }

        for (uint32_t i = 0; i < 2; ++i)
        {
            nvrhi::TextureDesc desc;
            desc.format = nvrhi::Format::R32_FLOAT;
            desc.debugName = "Exposure Readback Staging Texture";
            desc.initialState = nvrhi::ResourceStates::CopyDest;

            m_ExposureReadbackTextures[i] = g_Graphic.m_NVRHIDevice->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(float);
            desc.structStride = sizeof(float);
            desc.debugName = "Exposure Buffer";
            desc.canHaveTypedViews = true;
            desc.canHaveUAVs = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;

            g_Scene->m_LuminanceBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);

            const float kInitialExposure = 1.0f;
            commandList->writeBuffer(g_Scene->m_LuminanceBuffer, &kInitialExposure, sizeof(float));

            nvrhi::TextureDesc textureDesc;
            textureDesc.format = nvrhi::Format::R32_FLOAT;
            textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            textureDesc.isUAV = true;
            textureDesc.debugName = "Exposure Texture";

            g_Scene->m_ExposureTexture = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);
            commandList->writeTexture(g_Scene->m_ExposureTexture, 0, 0, &kInitialExposure, sizeof(float));
        }
    }
    
    bool HasImguiControls() const override { return true; }

    void UpdateImgui() override
    {
        bool bLuminanceDirty = false;

        ImGui::Text("Scene Luminance: %f", g_Scene->m_LastFrameLuminance);
        ImGui::Text("Scene Exposure: %f", g_Scene->m_LastFrameExposure);
        ImGui::DragFloat("Manual Exposure Override", &g_Scene->m_ManualExposureOverride, 0.1f, 0.0f);
        bLuminanceDirty |= ImGui::DragFloat("Minimum Luminance", &m_MinimumLuminance, 0.01f, 0.0f);
        bLuminanceDirty |= ImGui::DragFloat("Maximum Luminance", &m_MaximumLuminance, 0.01f, 0.0f);
        ImGui::DragFloat("Auto Exposure Speed", &m_AutoExposureSpeed, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Middle Gray", &g_Scene->m_MiddleGray, 0.01f, 0.0f, 0.99f);

        if (bLuminanceDirty)
        {
            m_MaximumLuminance = std::max(m_MaximumLuminance, m_MinimumLuminance + 0.1f);
        }
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (g_Scene->m_ManualExposureOverride > 0.0f)
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

        // read back previous frame's scene luminance
        {
            const float* readbackBytes = (float*)device->mapBuffer(m_LuminanceReadbackBuffers[g_Graphic.m_FrameCounter % 2], nvrhi::CpuAccessMode::Read);
            check(readbackBytes);
            g_Scene->m_LastFrameLuminance = *readbackBytes;
            device->unmapBuffer(m_LuminanceReadbackBuffers[g_Graphic.m_FrameCounter % 2]);
        }

        // read back previous frame's exposure value
        {
            nvrhi::StagingTextureHandle thisFrameExposureReadbackTexture = m_ExposureReadbackTextures[g_Graphic.m_FrameCounter % 2]; 

            size_t outRowPitch;
            const float* exposureReadback = (const float*)g_Graphic.m_NVRHIDevice->mapStagingTexture(thisFrameExposureReadbackTexture, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Read, &outRowPitch);
            check(exposureReadback);
            g_Scene->m_LastFrameExposure = *exposureReadback;
            device->unmapStagingTexture(thisFrameExposureReadbackTexture);
        }

        ON_EXIT_SCOPE_LAMBDA([&]
            {
                // copy to staging buffer/texture to be read back by CPU next frame, regardless whether manual exposure mode is enabled or not
                commandList->copyBuffer(m_LuminanceReadbackBuffers[g_Graphic.m_FrameCounter % 2], 0, g_Scene->m_LuminanceBuffer, 0, sizeof(float));
                commandList->copyTexture(m_ExposureReadbackTextures[g_Graphic.m_FrameCounter % 2], nvrhi::TextureSlice{}, g_Scene->m_ExposureTexture, nvrhi::TextureSlice{});
            });

        if (g_Scene->m_ManualExposureOverride > 0.0f)
        {
            commandList->writeBuffer(g_Scene->m_LuminanceBuffer, &g_Scene->m_ManualExposureOverride, sizeof(float));
            return;
        }

        const float minLogLum = std::log2(m_MinimumLuminance);
        const float maxLogLum = std::log2(m_MaximumLuminance);

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

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "adaptluminance_CS_GenerateLuminanceHistogram";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(passParameters.m_SrcColorDims, Vector2U{ 16, 16 });
            computePassParams.m_PushConstantsData = &passParameters;
            computePassParams.m_PushConstantsBytes = sizeof(passParameters);

            g_Graphic.AddComputePass(computePassParams);
        }

        // AdaptExposure
        {
            AdaptExposureParameters passParameters{};
            passParameters.m_AdaptationSpeed = std::clamp(m_AutoExposureSpeed * g_Engine.m_CPUCappedFrameTimeMs, 0.0f, 1.0f);
            passParameters.m_MinLogLuminance = minLogLum;
            passParameters.m_LogLuminanceRange = maxLogLum - minLogLum;
            passParameters.m_NbPixels = g_Graphic.m_RenderResolution.x * g_Graphic.m_RenderResolution.y;
            passParameters.m_MiddleGray = g_Scene->m_MiddleGray;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, luminanceHistogramBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, g_Scene->m_LuminanceBuffer),
                nvrhi::BindingSetItem::Texture_UAV(1, g_Scene->m_ExposureTexture)
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "adaptluminance_CS_AdaptExposure";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = Vector3U{ 1,1,1 };
            computePassParams.m_PushConstantsData = &passParameters;
            computePassParams.m_PushConstantsBytes = sizeof(passParameters);

            g_Graphic.AddComputePass(computePassParams);
        }
    }
};

static AdaptLuminanceRenderer gs_AdaptLuminanceRenderer;
IRenderer* g_AdaptLuminanceRenderer = &gs_AdaptLuminanceRenderer;
