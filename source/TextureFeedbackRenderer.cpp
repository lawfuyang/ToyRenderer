#include "Graphic.h"

#include "CommonResources.h"
#include "Engine.h"

#include "extern/imgui/imgui.h"

class TextureFeedbackRenderer : public IRenderer
{
    uint32_t m_SelectedTextureIdx = 0;
    bool m_bVisualizeStreamingStates = false;
    float m_Mip0Size = 400.0f;

public:
    TextureFeedbackRenderer() : IRenderer{ "TextureFeedbackRenderer" } {}

    void UpdateImgui() override
    {
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
        ImGui::SliderFloat("Mip 0 Size", &m_Mip0Size, 100.0f, 1000.0f);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return m_bVisualizeStreamingStates;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());

        const Texture& texture = g_Graphic.m_Textures.at(m_SelectedTextureIdx);

        float size = m_Mip0Size;
        const float margin = 10.0f;
        float x = margin;

        // mips
        for (uint32_t mip = 0; mip < texture.m_NVRHITextureHandle->getDesc().mipLevels; mip++)
        {
            const nvrhi::Viewport viewport{
                std::min((float)g_Graphic.m_DisplayResolution.x - 1.0f, x),
                std::min((float)g_Graphic.m_DisplayResolution.x - 1.0f, x + size),
                std::min((float)g_Graphic.m_DisplayResolution.y - 1.0f, g_Graphic.m_DisplayResolution.y - size - margin),
                std::min((float)g_Graphic.m_DisplayResolution.y - 1.0f, g_Graphic.m_DisplayResolution.y - margin),
                0.f, 1.f
            };

            x += size + margin;
            size *= 0.5f;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::Texture_SRV(0, texture.m_NVRHITextureHandle, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet{ mip, 1, 0, 1 }),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
            };

            Graphic::FullScreenPassParams fullScreenPassParams;
            fullScreenPassParams.m_CommandList = commandList;
            fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
            fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
            fullScreenPassParams.m_ShaderName = "fullscreen_PS_Passthrough";
            fullScreenPassParams.m_ViewPort = &viewport;

            g_Graphic.AddFullScreenPass(fullScreenPassParams);
        }

        // minmip
        {
            const nvrhi::Viewport viewport{
                std::min((float)g_Graphic.m_DisplayResolution.x - 1.0f, x),
                std::min((float)g_Graphic.m_DisplayResolution.x - 1.0f, x + m_Mip0Size),
                std::min((float)g_Graphic.m_DisplayResolution.y - 1.0f, g_Graphic.m_DisplayResolution.y - m_Mip0Size - margin),
                std::min((float)g_Graphic.m_DisplayResolution.y - 1.0f, g_Graphic.m_DisplayResolution.y - margin),
                0.f, 1.f
            };

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::Texture_SRV(0, texture.m_MinMipTextureHandle),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
            };

            Graphic::FullScreenPassParams fullScreenPassParams;
            fullScreenPassParams.m_CommandList = commandList;
            fullScreenPassParams.m_FrameBufferDesc = frameBufferDesc;
            fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
            fullScreenPassParams.m_ShaderName = "visualizeminmip_PS_VisualizeMinMip";
            fullScreenPassParams.m_ViewPort = &viewport;

            g_Graphic.AddFullScreenPass(fullScreenPassParams);
        }
    }
};
static TextureFeedbackRenderer gs_TextureFeedbackRenderer;
IRenderer* g_TextureFeedbackRenderer = &gs_TextureFeedbackRenderer;
