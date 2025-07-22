#include "Graphic.h"

#include "Engine.h"

#include "extern/imgui/imgui.h"

class TextureFeedbackRenderer : public IRenderer
{
    uint32_t m_SelectedTextureIdx = 0;
    bool m_bVisualizeStreamingStates = false;

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
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return m_bVisualizeStreamingStates;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        
    }
};
static TextureFeedbackRenderer gs_TextureFeedbackRenderer;
IRenderer* g_TextureFeedbackRenderer = &gs_TextureFeedbackRenderer;
