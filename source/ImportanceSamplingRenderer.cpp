#include "Graphic.h"

#include "Rtxdi/ImportanceSamplingContext.h"

class ImportanceSamplingRenderer : public IRenderer
{
    std::unique_ptr<rtxdi::ImportanceSamplingContext> m_ImportanceSamplingContext;

public:
    ImportanceSamplingRenderer()
        : IRenderer("Importance Sampling Renderer")
    {
    }

    ~ImportanceSamplingRenderer() override
    {
        m_ImportanceSamplingContext.reset();
    }

    void Initialize() override
    {
        rtxdi::ImportanceSamplingContext_StaticParameters isParams;
        isParams.renderWidth = g_Graphic.m_RenderResolution.x;
        isParams.renderHeight = g_Graphic.m_RenderResolution.y;

        m_ImportanceSamplingContext = std::make_unique<rtxdi::ImportanceSamplingContext>(isParams);
    }

    bool HasImguiControls() const { return false; }

    void UpdateImgui()
    {

    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return false;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {

    }
};

DEFINE_RENDERER(ImportanceSamplingRenderer);
