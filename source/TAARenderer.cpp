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
            LOG_DEBUG("NGX call failed: %s", StringUtils::WideToUtf8(GetNGXResultAsString(result))); \
            check(false); \
        } \
    }

class TAARenderer : public IRenderer
{
    bool m_bShutdownDone = false;
    NVSDK_NGX_Parameter* m_Parameters = nullptr;

public:
    TAARenderer() : IRenderer("TAA Renderer"){}

    ~TAARenderer() override
    {
        if (m_bShutdownDone)
        {
            return;
        }

        check(m_Parameters);
        NGX_CALL(NVSDK_NGX_D3D12_DestroyParameters(m_Parameters));
        m_Parameters = nullptr;
        
        ID3D12Device* natived3d12Device = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        NGX_CALL(NVSDK_NGX_D3D12_Shutdown1(natived3d12Device));

        m_bShutdownDone = true;
    }

    void Initialize() override
    {
        ID3D12Device* natived3d12Device = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        const char* projectID = "8f1e6c1e-83c7-44e1-9d35-5a55e26a7f74"; // MD5 hash of "ToyRenderer"
        const char* engineVersion = "1.0.0";
        NGX_CALL(NVSDK_NGX_D3D12_Init_with_ProjectID(projectID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engineVersion, StringUtils::Utf8ToWide(GetExecutableDirectory()), natived3d12Device));

        NGX_CALL(NVSDK_NGX_D3D12_GetCapabilityParameters(&m_Parameters));

        int bNeedsUpdatedDriver = 0;
        NGX_CALL(m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &bNeedsUpdatedDriver));

        if (bNeedsUpdatedDriver)
        {
            unsigned int minDriverVersionMajor = 0;
            unsigned int minDriverVersionMinor = 0;
            NGX_CALL(m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor));
            NGX_CALL(m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor));

            LOG_DEBUG("NVIDIA driver update required. Minimum driver version: %u.%u", minDriverVersionMajor, minDriverVersionMinor);
            check(false);
        }

        int bDLSS_Supported = 0;
        NGX_CALL(m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &bDLSS_Supported));
        check(bDLSS_Supported);

        int bDLSS_FeatureInitResult = 0;
        NGX_CALL(m_Parameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &bDLSS_FeatureInitResult));
        check(bDLSS_FeatureInitResult);
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
