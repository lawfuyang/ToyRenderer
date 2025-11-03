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
            const char* errorMessage = StringUtils::WideToUtf8(GetNGXResultAsString(result)); \
            SDL_Log("NGX call failed: %s", errorMessage); \
            SDL_free((void*)errorMessage); \
            check(false); \
        } \
    }

#define FFX_CALL(x) \
    { \
        const FfxApiReturnCodes result = (FfxApiReturnCodes)x; \
        if (result != FFX_API_RETURN_OK) \
        { \
            SDL_Log("FFX call failed: %s", EnumUtils::ToString(result)); \
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

    bool m_bDrawFSRDebugView = false;
    float m_FSRSharpening = 0.0f;

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

            SDL_Log("NVIDIA driver update required for DLSS. Minimum driver version: %u.%u", minDriverVersionMajor, minDriverVersionMinor);
            return;
        }

        int bDLSS_Supported = 0;
        NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &bDLSS_Supported));
        if (!bDLSS_Supported)
        {
            SDL_Log("DLSS not supported on this GPU");
            return;
        }

        int bDLSS_FeatureInitResult = 0;
        NGX_CALL(m_NGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &bDLSS_FeatureInitResult));
        if (!bDLSS_FeatureInitResult)
        {
            SDL_Log("DLSS feature init failed");
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
        SDL_Log("FFX %s: %s", type == FFX_API_MESSAGE_TYPE_ERROR ? "Error" : "Warning", msg);
        SDL_free((void*)msg);
        check(false);
    }

    void InitFSR()
    {
        ID3D12Device* nativeDevice = g_Graphic.m_NVRHIDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);

        const FfxApiDimensions2D renderResolution{ g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y };

        ffx::CreateContextDescUpscale createFsr{};
        createFsr.maxRenderSize = renderResolution;
        createFsr.maxUpscaleSize = renderResolution;
        createFsr.flags = 
            FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | 
            FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION |
            FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
            FFX_UPSCALE_ENABLE_DEPTH_INFINITE;

    #if 0
        createFsr.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;
    #endif

        createFsr.fpMessage = FfxMsgCallback;

        ffx::CreateBackendDX12Desc backendDesc{};
        backendDesc.device = nativeDevice;

        FFX_CALL(ffx::CreateContext(m_FSRContext, nullptr, createFsr, backendDesc));

        ffxQueryGetProviderVersion getVersion{};
        getVersion.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;

        FFX_CALL(ffxQuery(&m_FSRContext, &getVersion.header));
        SDL_Log("selected FSR Version: [%s]", getVersion.versionName);

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

        if (g_Scene->m_TAATechnique == TAATechnique::FSR)
        {
            ImGui::Checkbox("Draw FSR Debug View", &m_bDrawFSRDebugView);
            ImGui::SliderFloat("FSR Sharpening", &m_FSRSharpening, 0.0f, 1.0f);
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

    struct UpscalerNativeInputResources
    {
        ID3D12Resource* m_InColorResource = nullptr;
        ID3D12Resource* m_OutColorResource = nullptr;
        ID3D12Resource* m_DepthResource = nullptr;
        ID3D12Resource* m_MotionVectorsResource = nullptr;
        ID3D12Resource* m_ExposureResource = nullptr;
    };

    UpscalerNativeInputResources GetUpscalerNativeInputResources(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, bool bOutputRequiresUAVState = false)
    {
        UpscalerNativeInputResources resources;

        nvrhi::TextureHandle lightingOutputTexture = renderGraph.GetTexture(g_LightingOutputRDGTextureHandle);
        resources.m_InColorResource = lightingOutputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        nvrhi::TextureHandle upscaledLightingOutputTexture = renderGraph.GetTexture(g_AntiAliasedLightingOutputRDGTextureHandle);
        resources.m_OutColorResource = upscaledLightingOutputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        nvrhi::TextureHandle depthTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        resources.m_DepthResource = depthTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        nvrhi::TextureHandle motionVectorsTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
        resources.m_MotionVectorsResource = motionVectorsTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        resources.m_ExposureResource = g_Scene->m_ExposureTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

        if (bOutputRequiresUAVState)
        {
            commandList->setTextureState(upscaledLightingOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
            commandList->commitBarriers();
        }

        return resources;
    }
    
    void EvaluateDLSS(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        ID3D12GraphicsCommandList* nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

        const bool bOutputRequiresUAVState = true;
        const UpscalerNativeInputResources resources = GetUpscalerNativeInputResources(commandList, renderGraph, bOutputRequiresUAVState);

        const Vector2& jitterOffset = g_Scene->m_View.m_CurrentJitterOffset;

        NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};
        evalParams.Feature.pInColor = resources.m_InColorResource;
        evalParams.Feature.pInOutput = resources.m_OutColorResource;
        evalParams.pInDepth = resources.m_DepthResource;
        evalParams.pInMotionVectors = resources.m_MotionVectorsResource;
        evalParams.pInExposureTexture = resources.m_ExposureResource;
        evalParams.InJitterOffsetX = jitterOffset.x;
        evalParams.InJitterOffsetY = jitterOffset.y;
        evalParams.InRenderSubrectDimensions = NVSDK_NGX_Dimensions{ g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y };

        NGX_CALL(NGX_D3D12_EVALUATE_DLSS_EXT(nativeCommandList, m_NGXHandle, m_NGXParameters, &evalParams));
    }

    void EvaluateFSR(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        ID3D12GraphicsCommandList* nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        
        const bool bOutputRequiresUAVState = false;
        const UpscalerNativeInputResources resources = GetUpscalerNativeInputResources(commandList, renderGraph, bOutputRequiresUAVState);

        const FfxApiFloatCoords2D jitterOffset{ g_Scene->m_View.m_CurrentJitterOffset.x, g_Scene->m_View.m_CurrentJitterOffset.y };
        const FfxApiDimensions2D renderResolution{ g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y };

        // TODO: generate reactive mask

        ffx::DispatchDescUpscale dispatchUpscale{};
        dispatchUpscale.commandList = nativeCommandList;
        dispatchUpscale.color = ffxApiGetResourceDX12(resources.m_InColorResource, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatchUpscale.depth = ffxApiGetResourceDX12(resources.m_DepthResource, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatchUpscale.motionVectors = ffxApiGetResourceDX12(resources.m_MotionVectorsResource, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatchUpscale.exposure = ffxApiGetResourceDX12(resources.m_ExposureResource, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatchUpscale.reactive = ffxApiGetResourceDX12(nullptr);
        dispatchUpscale.transparencyAndComposition = ffxApiGetResourceDX12(nullptr);
        dispatchUpscale.output = ffxApiGetResourceDX12(resources.m_OutColorResource, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatchUpscale.jitterOffset = FfxApiFloatCoords2D{ jitterOffset.x, jitterOffset.y };
        dispatchUpscale.motionVectorScale = FfxApiFloatCoords2D{ 1.0f, 1.0f };
        dispatchUpscale.renderSize = renderResolution;
        dispatchUpscale.upscaleSize = renderResolution;
        dispatchUpscale.enableSharpening = m_FSRSharpening > 0.0f;
        dispatchUpscale.sharpness = m_FSRSharpening;
        dispatchUpscale.frameTimeDelta = g_Engine.m_CPUCappedFrameTimeMs;
        dispatchUpscale.preExposure = std::max(kKindaSmallNumber, g_Scene->m_LastFrameExposure);
        dispatchUpscale.reset = false;
        dispatchUpscale.cameraNear = FLT_MAX;
        dispatchUpscale.cameraFar = g_Scene->m_View.m_ZNearP;
        dispatchUpscale.cameraFovAngleVertical = g_Scene->m_View.m_FOV;
        dispatchUpscale.viewSpaceToMetersFactor = 0.0f; // ???
        dispatchUpscale.flags = m_bDrawFSRDebugView ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0;

        FFX_CALL(ffx::Dispatch(m_FSRContext, dispatchUpscale));
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        if (g_Scene->m_TAATechnique == TAATechnique::DLSS)
        {
            EvaluateDLSS(commandList, renderGraph);
        }
        else
        {
            EvaluateFSR(commandList, renderGraph);
        }
    }
};

static TAARenderer gs_TAARenderer;
IRenderer* g_TAARenderer = &gs_TAARenderer;
