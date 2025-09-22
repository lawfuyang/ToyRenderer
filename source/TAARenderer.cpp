#include "Graphic.h"

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Utilities.h"

#include "extern/imgui/imgui.h"

#define NGX_CALL(x) \
    { \
        const NVSDK_NGX_Result result = x; \
        if (NVSDK_NGX_FAILED(result)) \
        { \
            LOG_DEBUG("NGX call failed: %s", StringUtils::WideToUtf8(GetNGXResultAsString(result))); \
            check(false); \
        } \
    }

RenderGraph::ResourceHandle g_UpscaledLightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class TAARenderer : public IRenderer
{
    enum class TAATechnique { DLSS, FSR };

    TAATechnique m_TAATechnique = TAATechnique::DLSS;
    bool m_bShutdownDone = false;
    bool m_bDLSS_Supported = false;

    NVSDK_NGX_Parameter* m_NGXParameters = nullptr;
    NVSDK_NGX_Handle* m_NGXHandle = nullptr;

    struct DLSSOptimalSettings
    {
        unsigned int m_RenderOptimalWidth = 0;
        unsigned int m_RenderOptimalHeight = 0;
        unsigned int m_RenderMaxWidth = 0;
        unsigned int m_RenderMaxHeight = 0;
        unsigned int m_RenderMinWidth = 0;
        unsigned int m_RenderMinHeight = 0;
        float m_Sharpness = 0.0f;
    } m_DLSSOptimalSettings;

public:
    TAARenderer() : IRenderer("TAA Renderer"){}

    ~TAARenderer() override
    {
        if (m_bShutdownDone)
        {
            return;
        }

        check(m_NGXParameters);
        NGX_CALL(NVSDK_NGX_D3D12_DestroyParameters(m_NGXParameters));
        m_NGXParameters = nullptr;

        check(m_NGXHandle);
        NGX_CALL(NVSDK_NGX_D3D12_ReleaseFeature(m_NGXHandle));
        m_NGXHandle = nullptr;
        
        ID3D12Device* nativeDevice = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        NGX_CALL(NVSDK_NGX_D3D12_Shutdown1(nativeDevice));

        m_bShutdownDone = true;
    }

    void InitDLSS()
    {
        ID3D12Device* nativeDevice = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        const char* projectID = "8f1e6c1e-83c7-44e1-9d35-5a55e26a7f74"; // MD5 hash of "ToyRenderer"
        const char* engineVersion = "1.0.0";
        NGX_CALL(NVSDK_NGX_D3D12_Init_with_ProjectID(projectID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engineVersion, StringUtils::Utf8ToWide(GetExecutableDirectory()), nativeDevice));

        NGX_CALL(NVSDK_NGX_D3D12_GetCapabilityParameters(&m_NGXParameters));

        int bNeedsUpdatedDriver = 0;
        NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &bNeedsUpdatedDriver));

        if (bNeedsUpdatedDriver)
        {
            unsigned int minDriverVersionMajor = 0;
            unsigned int minDriverVersionMinor = 0;
            NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor));
            NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor));

            LOG_DEBUG("NVIDIA driver update required for DLSS. Minimum driver version: %u.%u", minDriverVersionMajor, minDriverVersionMinor);
            return;
        }

        int bDLSS_Supported = 0;
        NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &bDLSS_Supported));
        if (!bDLSS_Supported)
        {
            LOG_DEBUG("DLSS not supported on this GPU");
            return;
        }

        int bDLSS_FeatureInitResult = 0;
        NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &bDLSS_FeatureInitResult));
        if (!bDLSS_FeatureInitResult)
        {
            LOG_DEBUG("DLSS feature init failed");
            return;
        }

        const NVSDK_NGX_PerfQuality_Value perfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA; // just use DLAA for now

        NGX_CALL(NGX_DLSS_GET_OPTIMAL_SETTINGS(
            m_NGXParameters,
            g_Graphic.m_RenderResolution.x,
            g_Graphic.m_RenderResolution.y,
            perfQualityValue,
            &m_DLSSOptimalSettings.m_RenderOptimalWidth,
            &m_DLSSOptimalSettings.m_RenderOptimalHeight,
            &m_DLSSOptimalSettings.m_RenderMaxWidth,
            &m_DLSSOptimalSettings.m_RenderMaxHeight,
            &m_DLSSOptimalSettings.m_RenderMinWidth,
            &m_DLSSOptimalSettings.m_RenderMinHeight,
            &m_DLSSOptimalSettings.m_Sharpness
        ));

        check(m_DLSSOptimalSettings.m_RenderOptimalWidth > 0 && m_DLSSOptimalSettings.m_RenderOptimalHeight > 0);
        check(m_DLSSOptimalSettings.m_RenderOptimalWidth <= g_Graphic.m_RenderResolution.x);
        check(m_DLSSOptimalSettings.m_RenderOptimalHeight <= g_Graphic.m_RenderResolution.y);

        NVSDK_NGX_DLSS_Create_Params dlssCreateParams{};
        dlssCreateParams.Feature.InWidth = m_DLSSOptimalSettings.m_RenderOptimalWidth;
        dlssCreateParams.Feature.InHeight = m_DLSSOptimalSettings.m_RenderOptimalHeight;
        dlssCreateParams.Feature.InTargetWidth = g_Graphic.m_RenderResolution.x;
        dlssCreateParams.Feature.InTargetHeight = g_Graphic.m_RenderResolution.y;
        dlssCreateParams.Feature.InPerfQualityValue = perfQualityValue;
        dlssCreateParams.InFeatureCreateFlags =
            NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
            NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
            NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
            NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

        nvrhi::CommandListHandle cmdList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(cmdList, "Create DLSS");

        ID3D12GraphicsCommandList* nativeCommandList = cmdList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

        const unsigned int kCreationNodeMask = 1;
        const unsigned int kVisibilityNodeMask = 1;
        NGX_CALL(NGX_D3D12_CREATE_DLSS_EXT(nativeCommandList, kCreationNodeMask, kVisibilityNodeMask, &m_NGXHandle, m_NGXParameters, &dlssCreateParams));

        m_bDLSS_Supported = true;
    }

    void Initialize() override
    {
        InitDLSS();
    }

    void UpdateImgui() override
    {
        ImGui::Checkbox("Enable", &g_Scene->m_bEnableTAA);
        ImGui::Combo("Technique", reinterpret_cast<int*>(&m_TAATechnique), "DLSS\0FSR\0\0");
    }

    void OnRenderResolutionChanged() override
    {
        // TODO: upscaling
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Scene->m_bEnableTAA)
        {
            return false;
        }

        if (m_TAATechnique == TAATechnique::DLSS && !m_bDLSS_Supported)
        {
            return false;
        }
        else if (m_TAATechnique == TAATechnique::FSR)
        {
            g_Scene->m_bEnableTAA = false; // temp until start implementing FSR
            return false;
        }

        nvrhi::TextureDesc desc;
        desc.width = g_Graphic.m_RenderResolution.x;
        desc.height = g_Graphic.m_RenderResolution.y;
        desc.format = GraphicConstants::kLightingOutputFormat;
        desc.debugName = "Upscaled Lighting Output";
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        renderGraph.CreateTransientResource(g_UpscaledLightingOutputRDGTextureHandle, desc);

        renderGraph.AddReadDependency(g_LightingOutputRDGTextureHandle);
        renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferMotionRDGTextureHandle);

        return true;
    }
    
    void EvaluateDLSS(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        ID3D12GraphicsCommandList* nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

        nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
        ID3D12Resource* inColorResource = lightingOutputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        nvrhi::TextureHandle upscaledLightingOutputTexture = renderGraph.GetTexture(g_UpscaledLightingOutputRDGTextureHandle);
        ID3D12Resource* outColorResource = upscaledLightingOutputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        // output requires UAV state
        commandList->setTextureState(upscaledLightingOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();

        nvrhi::TextureHandle depthTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        ID3D12Resource* inDepthResource = depthTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        nvrhi::TextureHandle motionVectorsTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
        ID3D12Resource* inMotionVectorsResource = motionVectorsTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        ID3D12Resource* inExposureResource = g_Scene->m_ExposureTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        const Vector2& jitterOffset = g_Scene->m_View.m_CurrentJitterOffset;

        NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};
        evalParams.Feature.pInColor = inColorResource;
        evalParams.Feature.pInOutput = outColorResource;
        evalParams.pInDepth = inDepthResource;
        evalParams.pInMotionVectors = inMotionVectorsResource;
        evalParams.pInExposureTexture = inExposureResource;
        evalParams.InJitterOffsetX = jitterOffset.x;
        evalParams.InJitterOffsetY = jitterOffset.y;
        evalParams.InRenderSubrectDimensions = NVSDK_NGX_Dimensions{ g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y };

        NGX_CALL(NGX_D3D12_EVALUATE_DLSS_EXT(nativeCommandList, m_NGXHandle, m_NGXParameters, &evalParams));
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        if (m_TAATechnique == TAATechnique::DLSS && m_bDLSS_Supported)
        {
            EvaluateDLSS(commandList, renderGraph);
        }
        else
        {
            // TODO: FSR
        }
    }
};

static TAARenderer gs_TAARenderer;
IRenderer* g_TAARenderer = &gs_TAARenderer;
