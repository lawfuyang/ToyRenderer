#include "Graphic.h"

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#include "Engine.h"
#include "Utilities.h"

#define NGX_CALL(x) \
    { \
        const NVSDK_NGX_Result result = x; \
        if (NVSDK_NGX_FAILED(result)) \
        { \
            LOG_DEBUG("NGX call failed: %s", EnumUtils::ToString(result)); \
        } \
    }

class TAARenderer : public IRenderer
{
    bool m_bShutdownDone = false;
public:
    TAARenderer() : IRenderer("TAA Renderer"){}

    ~TAARenderer() override
    {
        if (m_bShutdownDone)
        {
            return;
        }
        
        ID3D12Device* natived3d12Device = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        NGX_CALL(NVSDK_NGX_D3D12_Shutdown1(natived3d12Device));

        m_bShutdownDone = true;
    }

    void Initialize() override
    {
        ID3D12Device* natived3d12Device = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        NGX_CALL(NVSDK_NGX_D3D12_Init(CompileTimeHashString64("ToyRenderer"), StringUtils::Utf8ToWide(GetExecutableDirectory()), natived3d12Device));
    }

    void UpdateImgui() override
    {

    }

    bool Setup(RenderGraph& renderGraph) override
    {
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {

    }
};

static TAARenderer gs_TAARenderer;
IRenderer* g_TAARenderer = &gs_TAARenderer;
