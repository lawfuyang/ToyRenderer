#include "Graphic.h"

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#include "extern/amd/FidelityFX2/Kits/FidelityFX/api/include/ffx_api.hpp"
#include "extern/amd/FidelityFX2/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.hpp"
#include "extern/amd/FidelityFX2/Kits/FidelityFX/upscalers/include/ffx_upscale.hpp"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"
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

#define FFX_CALL(x) \
    { \
        const FfxApiReturnCodes result = (FfxApiReturnCodes)x; \
        if (result != FFX_API_RETURN_OK) \
        { \
            LOG_DEBUG("FFX call failed: %s", EnumUtils::ToString(result)); \
            check(false); \
        } \
    }

RenderGraph::ResourceHandle g_AntiAliasedLightingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_LightingOutputRDGTextureHandle;

class TAARenderer : public IRenderer
{
    NVSDK_NGX_Parameter* m_NGXParameters = nullptr;
    NVSDK_NGX_Handle* m_NGXHandle = nullptr;
    ffx::Context m_FSRContext = nullptr;

public:
    TAARenderer() : IRenderer("TAA Renderer"){}

    ~TAARenderer() override
    {
        Shutdown();
    }

    void Shutdown()
    {
        if (m_NGXParameters)
        {
            NGX_CALL(NVSDK_NGX_D3D12_DestroyParameters(m_NGXParameters));
            m_NGXParameters = nullptr;
        }

        if (m_NGXHandle)
        {
            NGX_CALL(NVSDK_NGX_D3D12_ReleaseFeature(m_NGXHandle));
            m_NGXHandle = nullptr;

            ID3D12Device* nativeDevice = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
            NGX_CALL(NVSDK_NGX_D3D12_Shutdown1(nativeDevice));
        }

        if (m_FSRContext)
        {
            FFX_CALL(ffx::DestroyContext(m_FSRContext));
            m_FSRContext = nullptr;
        }
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

        unsigned int renderOptimalWidth = 0;
        unsigned int renderOptimalHeight = 0;
        unsigned int renderMaxWidth = 0;
        unsigned int renderMaxHeight = 0;
        unsigned int renderMinWidth = 0;
        unsigned int renderMinHeight = 0;
        float sharpness = 0.0f;

        NGX_CALL(NGX_DLSS_GET_OPTIMAL_SETTINGS(
            m_NGXParameters,
            g_Graphic.m_RenderResolution.x,
            g_Graphic.m_RenderResolution.y,
            perfQualityValue,
            &renderOptimalWidth,
            &renderOptimalHeight,
            &renderMaxWidth,
            &renderMaxHeight,
            &renderMinWidth,
            &renderMinHeight,
            &sharpness
        ));

        check(renderOptimalWidth > 0 && renderOptimalHeight > 0);
        check(renderOptimalWidth <= g_Graphic.m_RenderResolution.x);
        check(renderOptimalHeight <= g_Graphic.m_RenderResolution.y);

        NVSDK_NGX_DLSS_Create_Params dlssCreateParams{};
        dlssCreateParams.Feature.InWidth = renderOptimalWidth;
        dlssCreateParams.Feature.InHeight = renderOptimalHeight;
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

        g_Scene->m_bDLSS_Supported = true;
    }

    static void FfxMsgCallback(uint32_t type, const wchar_t* message)
    {
        const char* msg = StringUtils::WideToUtf8(message);
        LOG_DEBUG("FFX %s: %s", type == FFX_API_MESSAGE_TYPE_ERROR ? "Error" : "Warning", msg);
        check(false);
    }

    void InitFSR()
    {
        ID3D12Device* nativeDevice = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        uint64_t versionCount = 0;

        ffx::QueryDescGetVersions versionQuery{};
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versionQuery.device = nativeDevice;
        versionQuery.outputCount = &versionCount;

        FFX_CALL(ffxQuery(nullptr, &versionQuery.header));

        uint64_t FSRVersionIds[8]{};
        const char* FSRVersionNames[8]{};
        check(versionCount < std::size(FSRVersionIds));

        versionQuery.versionIds = FSRVersionIds;
        versionQuery.versionNames = FSRVersionNames;
        FFX_CALL(ffxQuery(nullptr, &versionQuery.header));

        std::string debugOutput;

        ffx::CreateContextDescOverrideVersion versionOverride{};
        ffx::QueryDescUpscaleGetResourceRequirements upscaleResourceRequirementsDesc{};
        for (size_t FSRVersionIndex = 0; FSRVersionIndex < versionCount; FSRVersionIndex++)
        {
            versionOverride.versionId = FSRVersionIds[FSRVersionIndex];
            FFX_CALL(ffx::Query(upscaleResourceRequirementsDesc, versionOverride));

            debugOutput += StringFormat("detected available FSR Version [%s]\n", FSRVersionNames[FSRVersionIndex]);
        }

        const FfxApiDimensions2D renderResolution{ g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y };

        ffx::CreateContextDescUpscale createFsr{};
        createFsr.maxRenderSize = renderResolution;
        createFsr.maxUpscaleSize = renderResolution;
        createFsr.flags = 
            FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | 
            FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS |
            FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION |
            FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
            FFX_UPSCALE_ENABLE_DEPTH_INFINITE |
        #if 1
            FFX_UPSCALE_ENABLE_DEBUG_CHECKING |
            FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;
        #else
            0;
        #endif
        createFsr.fpMessage = FfxMsgCallback;

        FfxApiEffectMemoryUsage gpuMemoryUsageUpscaler{};
        ffx::QueryDescUpscaleGetGPUMemoryUsageV2 upscalerGetGPUMemoryUsageV2{};
        upscalerGetGPUMemoryUsageV2.device = nativeDevice;
        upscalerGetGPUMemoryUsageV2.maxRenderSize = renderResolution;
        upscalerGetGPUMemoryUsageV2.maxUpscaleSize = renderResolution;
        upscalerGetGPUMemoryUsageV2.flags = createFsr.flags;
        upscalerGetGPUMemoryUsageV2.gpuMemoryUsageUpscaler = &gpuMemoryUsageUpscaler;

        ffx::CreateBackendDX12Desc backendDesc{};
        backendDesc.device = nativeDevice;

        FFX_CALL(ffx::Query(upscalerGetGPUMemoryUsageV2));
        debugOutput += StringFormat("FSR totalUsageInBytes [%f MB] aliasableUsageInBytes [%f MB]\n", BYTES_TO_MB(gpuMemoryUsageUpscaler.totalUsageInBytes), BYTES_TO_MB(gpuMemoryUsageUpscaler.aliasableUsageInBytes));
        FFX_CALL(ffx::CreateContext(m_FSRContext, nullptr, createFsr, backendDesc));

        ffxQueryGetProviderVersion getVersion{};
        getVersion.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;

        FFX_CALL(ffxQuery(&m_FSRContext, &getVersion.header));
        debugOutput += StringFormat("auto-selected FSR Version: [%s]", getVersion.versionName);

        LOG_DEBUG("%s", debugOutput.c_str());

        ffx::ConfigureDescGlobalDebug1 globalDebugConfig{};
        globalDebugConfig.debugLevel = 0; // not implemented. Value doesn't matter.
        globalDebugConfig.fpMessage = FfxMsgCallback;

        FFX_CALL(ffx::Configure(m_FSRContext, globalDebugConfig));
    }

    void Initialize() override
    {
        if (g_Scene->m_TAATechnique == TAATechnique::DLSS)
        {
            InitDLSS();
        }
        else if (g_Scene->m_TAATechnique == TAATechnique::FSR)
        {
            InitFSR();
        }
    }

    bool HasImguiControls() const override { return true; }

    void UpdateImgui() override
    {
        if (ImGui::Combo("Technique", reinterpret_cast<int*>(&g_Scene->m_TAATechnique), "None\0DLSS\0FSR\0\0"))
        {
            verify(g_Graphic.m_NVRHIDevice->waitForIdle());

            Shutdown();
            Initialize();
        }
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Scene->IsTAAEnabled())
        {
            return false;
        }

        nvrhi::TextureDesc desc;
        desc.width = g_Graphic.m_RenderResolution.x;
        desc.height = g_Graphic.m_RenderResolution.y;
        desc.format = GraphicConstants::kLightingOutputFormat;
        desc.debugName = "Anti-Aliased Lighting Output";
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        renderGraph.CreateTransientResource(g_AntiAliasedLightingOutputRDGTextureHandle, desc);

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

        nvrhi::TextureHandle upscaledLightingOutputTexture = renderGraph.GetTexture(g_AntiAliasedLightingOutputRDGTextureHandle);
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
        if (g_Scene->m_TAATechnique == TAATechnique::DLSS)
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
