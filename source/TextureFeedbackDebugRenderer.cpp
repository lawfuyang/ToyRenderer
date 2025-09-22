#include "Graphic.h"

#include "CommonResources.h"
#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

#include "extern/imgui/imgui.h"

class TextureFeedbackDebugRenderer : public IRenderer
{
    enum class DebugMode { TextureMips, FeedbackAndMinMip };

    DebugMode m_DebugMode = DebugMode::TextureMips;
    uint32_t m_SelectedTextureIdx = 0;
    bool m_bVisualizeStreamingStates = false;
    bool m_bVisualizeWithColorOnly = false;
    float m_ZoomLevel = 512.0f;

    RenderGraph::ResourceHandle m_FeedbackTextureHandle;

public:
    TextureFeedbackDebugRenderer() : IRenderer{ "TextureFeedbackDebugRenderer" } {}

    bool HasImguiControls() const override { return true; }

    void UpdateImgui() override
    {
        ImGui::Checkbox("Visualize Min Mip Tiles", &g_Scene->m_bVisualizeMinMipTilesOnAlbedoOutput);

        if (ImGui::BeginCombo("Texture to Preview", g_Graphic.m_Textures[m_SelectedTextureIdx].m_NVRHITextureHandle->getDesc().debugName.c_str(), ImGuiComboFlags_None))
        {
            for (uint32_t i = 0; i < g_Graphic.m_Textures.size(); i++)
            {
                const bool bIsSelected = (m_SelectedTextureIdx == i);
                if (ImGui::Selectable(g_Graphic.m_Textures[i].m_NVRHITextureHandle->getDesc().debugName.c_str(), bIsSelected))
                {
                    m_SelectedTextureIdx = i;
                }

                if (bIsSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Checkbox("Visualize Streaming States", &m_bVisualizeStreamingStates);
        ImGui::Combo("Debug Mode", reinterpret_cast<int*>(&m_DebugMode), "Texture Mips\0Feedback and Min Mip\0");

        if (m_DebugMode == DebugMode::FeedbackAndMinMip)
        {
            ImGui::Checkbox("Visualize with Color Only", &m_bVisualizeWithColorOnly);
        }
        
        ImGui::SliderFloat("Zoom Level", &m_ZoomLevel, 100.0f, 1000.0f);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!m_bVisualizeStreamingStates)
        {
            return false;
        }

        if (m_DebugMode == DebugMode::FeedbackAndMinMip)
        {
            renderGraph.CreateTransientResource(m_FeedbackTextureHandle, g_Graphic.m_Textures[m_SelectedTextureIdx].m_MinMipTextureHandle->getDesc());
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());

        const Texture& texture = g_Graphic.m_Textures.at(m_SelectedTextureIdx);

        float size = m_ZoomLevel;
        const float margin = 10.0f;
        float x = margin;

        // mips
        if (m_DebugMode == DebugMode::TextureMips)
        {
            for (uint32_t mip = 0; mip < texture.m_NVRHITextureHandle->getDesc().mipLevels; mip++)
            {
                const nvrhi::Viewport viewport{
                    std::min((float)g_Graphic.m_RenderResolution.x - 1.0f, x),
                    std::min((float)g_Graphic.m_RenderResolution.x - 1.0f, x + size),
                    std::min((float)g_Graphic.m_RenderResolution.y - 1.0f, g_Graphic.m_RenderResolution.y - size - margin),
                    std::min((float)g_Graphic.m_RenderResolution.y - 1.0f, g_Graphic.m_RenderResolution.y - margin),
                    0.f, 1.f
                };

                x += size + margin;
                size *= 0.5f;

                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings =
                {
                    nvrhi::BindingSetItem::Texture_SRV(0, texture.m_NVRHITextureHandle, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ mip, 1, 0, 1 }),
                    nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampMaxReductionSampler)
                };

                Graphic::FullScreenPassParams fullScreenPassParams;
                fullScreenPassParams.m_CommandList = commandList;
                fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
                fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
                fullScreenPassParams.m_ShaderName = "fullscreen_PS_Passthrough";
                fullScreenPassParams.m_ViewPort = &viewport;

                g_Graphic.AddFullScreenPass(fullScreenPassParams);
            }
        }
        else if (m_DebugMode == DebugMode::FeedbackAndMinMip)
        {
            auto VisualizeCommon = [&](nvrhi::TextureHandle inputTexture)
                {
                    VisualizeMinMipParameters passParameters;
                    passParameters.m_TextureDimensions = Vector2U{ inputTexture->getDesc().width, inputTexture->getDesc().height };
                    passParameters.m_bVisualizeWithColorOnly = m_bVisualizeWithColorOnly;

                    nvrhi::BindingSetDesc bindingSetDesc;
                    bindingSetDesc.bindings =
                    {
                        nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                        nvrhi::BindingSetItem::Texture_SRV(0, inputTexture),
                        nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampMaxReductionSampler)
                    };

                    const nvrhi::Viewport viewport{
                        std::min((float)g_Graphic.m_RenderResolution.x - 1.0f, x),
                        std::min((float)g_Graphic.m_RenderResolution.x - 1.0f, x + m_ZoomLevel),
                        std::min((float)g_Graphic.m_RenderResolution.y - 1.0f, g_Graphic.m_RenderResolution.y - m_ZoomLevel - margin),
                        std::min((float)g_Graphic.m_RenderResolution.y - 1.0f, g_Graphic.m_RenderResolution.y - margin),
                        0.f, 1.f
                    };

                    Graphic::FullScreenPassParams fullScreenPassParams;
                    fullScreenPassParams.m_CommandList = commandList;
                    fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
                    fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
                    fullScreenPassParams.m_ShaderName = "visualizeminmip_PS_VisualizeMinMip";
                    fullScreenPassParams.m_ViewPort = &viewport;
                    fullScreenPassParams.m_PushConstantsData = &passParameters;
                    fullScreenPassParams.m_PushConstantsBytes = sizeof(passParameters);

                    g_Graphic.AddFullScreenPass(fullScreenPassParams);
            };

            // feedback
            {
                nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

                nvrhi::TextureHandle feedbackTextureHandle = renderGraph.GetTexture(m_FeedbackTextureHandle);

                nvrhi::BufferHandle resolveBuffer = texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2];
                void* pReadbackData = device->mapBuffer(resolveBuffer, nvrhi::CpuAccessMode::Read);

                std::vector<uint8_t> feedbackData;
                feedbackData.resize(resolveBuffer->getDesc().byteSize);
                memcpy(feedbackData.data(), pReadbackData, feedbackData.size());
                device->unmapBuffer(resolveBuffer);

                commandList->writeTexture(feedbackTextureHandle, 0, 0, feedbackData.data(), feedbackTextureHandle->getDesc().width);

                VisualizeCommon(feedbackTextureHandle);
            }

            x += m_ZoomLevel + margin;

            // minmip
            {
                VisualizeCommon(texture.m_MinMipTextureHandle);
            }
        }
    }
};
static TextureFeedbackDebugRenderer gs_TextureFeedbackDebugRenderer;
IRenderer* g_TextureFeedbackDebugRenderer = &gs_TextureFeedbackDebugRenderer;
