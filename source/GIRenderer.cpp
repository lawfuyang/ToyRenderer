#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "shaders/DDGIShaderConfig.h"
#include "rtxgi/ddgi/DDGIVolume.h"

#include "Scene.h"
#include "GraphicPropertyGrid.h"

#include "shaders/ShaderInterop.h"

static_assert(RTXGI_DDGI_WAVE_LANE_COUNT == kNumThreadsPerWave);

class GIRenderer : public IRenderer
{
public:
    GIRenderer() : IRenderer("GIRenderer") {}

    void Initialize() override
    {
        Scene* scene = g_Graphic.m_Scene.get();

        nvrhi::TextureDesc desc;
        desc.width = g_Graphic.m_RenderResolution.x;
        desc.height = g_Graphic.m_RenderResolution.y;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.isUAV = true;
        desc.debugName = "DDGI Output";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        scene->m_DDGIOutput = g_Graphic.m_NVRHIDevice->createTexture(desc);
    }

    void UpdateImgui() override
    {
        auto& params = g_GraphicPropertyGrid.m_LightingControllables;

        ImGui::Checkbox("Enable DDGI", &params.m_EnableDDGI);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        const auto& lightingControllables = g_GraphicPropertyGrid.m_LightingControllables;

        if (!lightingControllables.m_EnableDDGI)
        {
            return false;
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {

    }
};
static GIRenderer gs_GIRenderer;
IRenderer* g_GIRenderer = &gs_GIRenderer;
