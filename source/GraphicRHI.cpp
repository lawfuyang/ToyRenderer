#include "GraphicRHI.h"

#include "extern/nvrhi/include/nvrhi/d3d12.h"
#include "extern/nvrhi/include/nvrhi/validation.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_properties.h"

#include "Engine.h"
#include "Graphic.h"
#include "Utilities.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\"; }

CommandLineOption<bool> g_EnableGraphicRHIValidation{ "graphicrhivalidation", false };
CommandLineOption<bool> g_EnableGPUValidation{ "enablegpuvalidation", false };

PRAGMA_OPTIMIZE_OFF;
class NVRHIMessageCallback : public nvrhi::IMessageCallback
{
    // Inherited via IMessageCallback
    void message(nvrhi::MessageSeverity severity, const char* messageText) override
    {
        LOG_DEBUG("[NVRHI]: %s", messageText);

        switch (severity)
        {
            // just print info messages
        case nvrhi::MessageSeverity::Info:
            break;

            // treat everything else critically
        case nvrhi::MessageSeverity::Warning:
        case nvrhi::MessageSeverity::Error:
        case nvrhi::MessageSeverity::Fatal:
            assert(false);
            break;
        }
    }
};
static NVRHIMessageCallback g_NVRHIErrorCB;
PRAGMA_OPTIMIZE_ON;

// for D3D12MAAllocator creation in nvrhi d3d12 device ctor
IDXGIAdapter1* g_DXGIAdapter;

nvrhi::DeviceHandle D3D12RHI::CreateDevice()
{
    {
        PROFILE_SCOPED("CreateDXGIFactory");

        const UINT factoryFlags = g_EnableGraphicRHIValidation.Get() ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT_CALL(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_DXGIFactory)));
    }

    {
        PROFILE_SCOPED("Get Adapters");

        for (UINT i = 0; m_DXGIFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_DXGIAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            m_DXGIAdapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't bother with the Basic Render Driver adapter.
                continue;
            }

            const char* gpuName = StringUtils::WideToUtf8(desc.Description);
            LOG_DEBUG("Graphic Adapter: %s", gpuName);
            break;
        }
        assert(m_DXGIAdapter);

        g_DXGIAdapter = m_DXGIAdapter.Get();
    }

    ComPtr<ID3D12Debug3> debugInterface;
    HRESULT_CALL(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    if (g_EnableGraphicRHIValidation.Get())
    {
        // enable DRED
        // NOTE: RenderDoc <= 1.37 doesnt like this
        if (!g_Graphic.m_RenderDocAPI)
        {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
            HRESULT_CALL(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

            // Turn on auto-breadcrumbs and page fault reporting.
            pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }

        debugInterface->EnableDebugLayer();
        LOG_DEBUG("D3D12 Debug Layer enabled");

        if (g_EnableGPUValidation.Get())
        {
            debugInterface->SetEnableGPUBasedValidation(true);
            LOG_DEBUG("D3D12 GPU Based Validation enabled");
        }
    }

    {
        PROFILE_SCOPED("D3D12CreateDevice");

        // enforce requirment of 12_0 feature level at least
        static const D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0;
        HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, kMinimumFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));

        // copied from d3dx12.h
        static auto QueryHighestFeatureLevel = [](ID3D12Device* device)
            {
                // Check against a list of all feature levels present in d3dcommon.h
                // Needs to be updated for future feature levels
                const D3D_FEATURE_LEVEL allLevels[] =
                {
            #if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 3)
                    D3D_FEATURE_LEVEL_12_2,
            #endif
                    D3D_FEATURE_LEVEL_12_1,
                    D3D_FEATURE_LEVEL_12_0,
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1,
                    D3D_FEATURE_LEVEL_10_0,
                    D3D_FEATURE_LEVEL_9_3,
                    D3D_FEATURE_LEVEL_9_2,
                    D3D_FEATURE_LEVEL_9_1,
            #if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 5)
                    D3D_FEATURE_LEVEL_1_0_CORE,
            #endif
            #if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 611)
                    D3D_FEATURE_LEVEL_1_0_GENERIC
            #endif
                };

                D3D12_FEATURE_DATA_FEATURE_LEVELS dFeatureLevel;
                dFeatureLevel.NumFeatureLevels = static_cast<UINT>(sizeof(allLevels) / sizeof(D3D_FEATURE_LEVEL));
                dFeatureLevel.pFeatureLevelsRequested = allLevels;

                const HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &dFeatureLevel, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS));
                assert(SUCCEEDED(result));

                return dFeatureLevel.MaxSupportedFeatureLevel;
            };

        const D3D_FEATURE_LEVEL maxSupportedFeatureLevel = QueryHighestFeatureLevel(m_D3DDevice.Get());

        // use higher feature level if available
        if (maxSupportedFeatureLevel != kMinimumFeatureLevel)
        {
            HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, maxSupportedFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));
        }

        LOG_DEBUG("Initialized D3D12 Device with feature level: 0x%X", maxSupportedFeatureLevel);

        // break on warnings/errors
        ComPtr<ID3D12InfoQueue> debugInfoQueue;
        HRESULT_CALL(m_D3DDevice->QueryInterface(__uuidof(ID3D12InfoQueue), (LPVOID*)&debugInfoQueue));

        // NOTE: add whatever d3d12 filters here when needed
        D3D12_INFO_QUEUE_FILTER newFilter{};

        // Turn off info msgs as these get really spewy
        D3D12_MESSAGE_SEVERITY denySeverity = D3D12_MESSAGE_SEVERITY_INFO;
        newFilter.DenyList.NumSeverities = 1;
        newFilter.DenyList.pSeverityList = &denySeverity;

        D3D12_MESSAGE_ID denyIds[] =
        {
            D3D12_MESSAGE_ID_HEAP_ADDRESS_RANGE_INTERSECTS_MULTIPLE_BUFFERS,
            // D3D12 complains when a buffer is created with a specific initial resource state while all buffers are currently created in COMMON state.
            // The next transition is then done use state promotion. It's just a warning and we need to keep track of the correct initial state as well for upcoming internal transitions.
            D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED
        };

        newFilter.DenyList.NumIDs = (UINT)std::size(denyIds);
        newFilter.DenyList.pIDList = denyIds;

        HRESULT_CALL(debugInfoQueue->PushStorageFilter(&newFilter));
        HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
        HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true));
        HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
    }

    auto CreateQueue = [this](nvrhi::CommandQueue queue)
        {
            PROFILE_SCOPED("CreateQueue");

            const char* queueName = nvrhi::utils::CommandQueueToString(queue);
            PROFILE_SCOPED(queueName);

            static const D3D12_COMMAND_LIST_TYPE kD3D12QueueTypes[] = { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY };

            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = kD3D12QueueTypes[(uint32_t)queue];
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0; // For single-adapter, set to 0. Else, set a bit to identify the node

            ComPtr<ID3D12CommandQueue> outQueue;
            HRESULT_CALL(m_D3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&outQueue)));

            HRESULT_CALL(outQueue->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(queueName), queueName));

            g_Graphic.m_GPUQueueLogs[(uint32_t)queue] = MicroProfileInitGpuQueue(queueName);

            return outQueue;
        };

    m_GraphicsQueue = CreateQueue(nvrhi::CommandQueue::Graphics);
    //m_ComputeQueue = CreateQueue(nvrhi::CommandQueue::Compute);
    //m_CopyQueue = CreateQueue(nvrhi::CommandQueue::Copy);

    void* pCommandQueues[] = { m_GraphicsQueue.Get() };
    MicroProfileGpuInitD3D12(m_D3DDevice.Get(), 1, pCommandQueues);
    MicroProfileSetCurrentNodeD3D12(0);

    nvrhi::d3d12::DeviceDesc deviceDesc;
    deviceDesc.errorCB = &g_NVRHIErrorCB;
    deviceDesc.pDevice = m_D3DDevice.Get();
    deviceDesc.pGraphicsCommandQueue = m_GraphicsQueue.Get();
    deviceDesc.pComputeCommandQueue = m_ComputeQueue.Get();
    deviceDesc.pCopyCommandQueue = m_CopyQueue.Get();
    deviceDesc.enableHeapDirectlyIndexed = true;

    nvrhi::DeviceHandle device = nvrhi::d3d12::createDevice(deviceDesc);

    if (g_EnableGraphicRHIValidation.Get())
    {
        device = nvrhi::validation::createValidationLayer(device);
    }

    return device;
}

void D3D12RHI::InitSwapChainTextureHandles()
{
    PROFILE_FUNCTION();

    BOOL tearingSupported{};
    HRESULT_CALL(m_DXGIFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported)));
    m_bTearingSupported = tearingSupported;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
    SwapChainDesc.Width = g_Graphic.m_DisplayResolution.x;
    SwapChainDesc.Height = g_Graphic.m_DisplayResolution.y;
    SwapChainDesc.Format = nvrhi::d3d12::convertFormat(nvrhi::Format::RGBA8_UNORM); // TODO: HDR display support
    SwapChainDesc.Stereo = false; // set to true for VR
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = _countof(m_SwapChainD3D12Resources);
    SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // TODO: Learn the differences
    SwapChainDesc.SampleDesc.Count = 1; // >1 valid only with bit-block transfer (bitblt) model swap chains.
    SwapChainDesc.Flags = m_bTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ::HWND hwnd = (::HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(g_Engine.m_SDLWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

    ComPtr<IDXGISwapChain1> SwapChain;
    HRESULT_CALL(m_DXGIFactory->CreateSwapChainForHwnd(m_GraphicsQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
                                                       hwnd,
                                                       &SwapChainDesc,
                                                       nullptr,
                                                       nullptr,
                                                       &SwapChain));

    // Disable Alt-Enter and other DXGI trickery...
    HRESULT_CALL(m_DXGIFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER));

    HRESULT_CALL(SwapChain.As(&m_SwapChain));
    SwapChain->QueryInterface(IID_PPV_ARGS(&m_SwapChain));

    // create textures for swap chain
    for (uint32_t i = 0; i < _countof(m_SwapChainD3D12Resources); ++i)
    {
        HRESULT_CALL(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainD3D12Resources[i])));

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = SwapChainDesc.Width;
        textureDesc.height = SwapChainDesc.Height;
        textureDesc.format = nvrhi::Format::RGBA8_UNORM;
        textureDesc.debugName = "SwapChainBuffer";
        textureDesc.isRenderTarget = true;
        textureDesc.initialState = nvrhi::ResourceStates::Present;

        g_Graphic.m_SwapChainTextureHandles[i] = g_Graphic.m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object{ m_SwapChainD3D12Resources[i].Get() }, textureDesc);
    }
}

void D3D12RHI::SwapChainPresent()
{
    PROFILE_FUNCTION();

    const UINT kSyncInterval = 0; // 0: no vsync, 1: vsync

    // When using sync interval 0, it is recommended to always pass the tearing flag when it is supported.
    const UINT kFlags = (kSyncInterval == 0 && m_bTearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    // Present the frame.
    const HRESULT presentResult = m_SwapChain->Present(kSyncInterval, kFlags);

    if (FAILED(presentResult))
    {
        verify(g_Graphic.m_NVRHIDevice->waitForIdle());
        assert(0);
    }
}

void* D3D12RHI::GetNativeCommandListType(nvrhi::CommandListHandle commandList)
{
    return commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
}

void D3D12RHI::SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName)
{
    ID3D12GraphicsCommandList* D3D12CommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    HRESULT_CALL(D3D12CommandList->SetPrivateData(WKPDID_D3DDebugObjectName, debugName.size(), debugName.data()));
}

void D3D12RHI::SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName)
{
    ID3D12Resource* D3D12Resource = resource->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    HRESULT_CALL(D3D12Resource->SetPrivateData(WKPDID_D3DDebugObjectName, debugName.size(), debugName.data()));
}
