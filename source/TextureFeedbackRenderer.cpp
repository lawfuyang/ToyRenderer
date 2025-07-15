#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"

#include "shaders/ShaderInterop.h"

class TextureFeedbackRenderer : public IRenderer
{
public:
    TextureFeedbackRenderer() : IRenderer("TextureFeedbackRenderer") {}

    void Initialize() override
    {
        
    }

    void UpdateImgui() override
    {
        
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return false; // TODO
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        
    }
};
static TextureFeedbackRenderer gs_TextureFeedbackRenderer;
IRenderer* g_TextureFeedbackRenderer = &gs_TextureFeedbackRenderer;
