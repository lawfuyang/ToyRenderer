#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

class TextureFeedbackRenderer : public IRenderer
{
public:
    TextureFeedbackRenderer() : IRenderer("TextureFeedbackRenderer") {}

    void UpdateImgui() override
    {
        ImGui::SliderInt("Feedback Textures to Resolve Per Frame", &g_Scene->m_NumFeedbackTexturesToResolvePerFrame, 1, g_Graphic.m_Textures.size());
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return !g_Graphic.m_Textures.empty();
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        {
            PROFILE_GPU_SCOPED(commandList, "Resolve Sampler Feedback Textures");

            for (uint32_t i = 0; i < g_Scene->m_NumFeedbackTexturesToResolvePerFrame; ++i)
            {
                const uint32_t textureIdx = (g_Scene->m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
                Texture& texture = g_Graphic.m_Textures[textureIdx];

                commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
            }
        }
    }
};
static TextureFeedbackRenderer gs_TextureFeedbackRenderer;
IRenderer* g_TextureFeedbackRenderer = &gs_TextureFeedbackRenderer;
