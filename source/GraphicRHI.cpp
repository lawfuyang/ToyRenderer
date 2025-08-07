#include <d3d12.h>
#include <dxgi1_6.h>

#include "volk.h"

#include "extern/nvrhi/include/nvrhi/d3d12.h"
#include "extern/nvrhi/include/nvrhi/vulkan.h"
#include "extern/nvrhi/include/nvrhi/validation.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_properties.h"

#include "Engine.h"
#include "Graphic.h"
#include "Utilities.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\"; }

CommandLineOption<bool> g_CVarUseVulkanRHI{ "usevulkanrhi", false };
CommandLineOption<bool> g_CVarEnableGraphicRHIValidation{ "graphicrhivalidation", false };
CommandLineOption<bool> g_CVarEnableGPUValidation{ "enablegpuvalidation", false };

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

inline thread_local HRESULT tl_HResult;
#define HRESULT_CALL(call)           \
    do                               \
    {                                \
        tl_HResult = (call);         \
        assert(!FAILED(tl_HResult)); \
    } while (0)

// for D3D12MAAllocator creation in nvrhi d3d12 device ctor
IDXGIAdapter1* g_DXGIAdapter;

class D3D12RHI : public GraphicRHI
{
public:
    nvrhi::DeviceHandle CreateDevice() override
    {
        {
            PROFILE_SCOPED("CreateDXGIFactory");

            const UINT factoryFlags = g_CVarEnableGraphicRHIValidation.Get() ? DXGI_CREATE_FACTORY_DEBUG : 0;
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

                const char *gpuName = StringUtils::WideToUtf8(desc.Description);
                LOG_DEBUG("Graphic Adapter: %s", gpuName);
                break;
            }
            assert(m_DXGIAdapter);

            HRESULT_CALL(m_DXGIAdapter->QueryInterface(IID_PPV_ARGS(&m_DXGIAdapter3)));

            g_DXGIAdapter = m_DXGIAdapter.Get();
        }

        if (g_CVarEnableGraphicRHIValidation.Get())
        {
            ComPtr<ID3D12Debug6> debugInterface;
            HRESULT_CALL(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));

            debugInterface->EnableDebugLayer();
            LOG_DEBUG("D3D12 Debug Layer enabled");

            if (g_CVarEnableGPUValidation.Get())
            {
                debugInterface->SetEnableGPUBasedValidation(true);
                LOG_DEBUG("D3D12 GPU Based Validation enabled");
            }

            debugInterface->SetEnableAutoName(true);
        }

        {
            PROFILE_SCOPED("D3D12CreateDevice");

            static const D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_12_2;
            HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, kFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));

            LOG_DEBUG("Initialized D3D12 Device with feature level: 0x%X", kFeatureLevel);

            // break on warnings/errors
            if (g_CVarEnableGraphicRHIValidation.Get())
            {
                ComPtr<ID3D12InfoQueue1> debugInfoQueue;
                HRESULT_CALL(m_D3DDevice->QueryInterface(__uuidof(ID3D12InfoQueue1), (LPVOID *)&debugInfoQueue));

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
                        D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED};

                newFilter.DenyList.NumIDs = (UINT)std::size(denyIds);
                newFilter.DenyList.pIDList = denyIds;

                HRESULT_CALL(debugInfoQueue->PushStorageFilter(&newFilter));
                HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
                HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true));
                HRESULT_CALL(debugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
            }
        }

        auto CreateQueue = [this](nvrhi::CommandQueue queue)
        {
            PROFILE_SCOPED("CreateQueue");

            const char *queueName = nvrhi::utils::CommandQueueToString(queue);
            PROFILE_SCOPED(queueName);

            static const D3D12_COMMAND_LIST_TYPE kD3D12QueueTypes[] = {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY};

            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = kD3D12QueueTypes[(uint32_t)queue];
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0; // For single-adapter, set to 0. Else, set a bit to identify the node

            ComPtr<ID3D12CommandQueue> outQueue;
            HRESULT_CALL(m_D3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&outQueue)));

            HRESULT_CALL(outQueue->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(queueName), queueName));

            return outQueue;
        };

        m_GraphicsQueue = CreateQueue(nvrhi::CommandQueue::Graphics);
        // m_ComputeQueue = CreateQueue(nvrhi::CommandQueue::Compute);
        // m_CopyQueue = CreateQueue(nvrhi::CommandQueue::Copy);

        void *pCommandQueues[] = {m_GraphicsQueue.Get()};
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

        // TODO: in release mode, the device ALWAYS gets released on launch when nvrhi validation layer is not used. did not investigate, and i frankly dont care
        //if (g_CVarEnableGraphicRHIValidation.Get())
        {
            device = nvrhi::validation::createValidationLayer(device);
        }

        return device;
    }

    void InitSwapChainTextureHandles() override
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
        SwapChainDesc.Stereo = false;
        SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        SwapChainDesc.BufferCount = _countof(m_SwapChainD3D12Resources);
        SwapChainDesc.Scaling = DXGI_SCALING_NONE;
        SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        SwapChainDesc.SampleDesc.Count = 1;
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

            g_Graphic.m_SwapChainTextureHandles[i] = g_Graphic.m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object{m_SwapChainD3D12Resources[i].Get()}, textureDesc);
        }
    }

    uint32_t GetCurrentBackBufferIndex() override { return m_SwapChain->GetCurrentBackBufferIndex(); }

    void SwapChainPresent() override
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

    void* GetNativeCommandList(nvrhi::CommandListHandle commandList) override
    {
        return commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }

    uint32_t GetTiledResourceSizeInBytes() override { return D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES; }

    uint32_t GetMaxTextureDimension() override { return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION; }

    uint32_t GetMaxNumTextureMips() override { return ComputeNbMips(GetMaxTextureDimension(), GetMaxTextureDimension()); }

    uint32_t GetMaxThreadGroupsPerDimension() override { return D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION; }

    void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) override
    {
        ID3D12GraphicsCommandList *D3D12CommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        HRESULT_CALL(D3D12CommandList->SetPrivateData(WKPDID_D3DDebugObjectName, debugName.size(), debugName.data()));
    }

    void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) override
    {
        ID3D12Resource *D3D12Resource = resource->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        HRESULT_CALL(D3D12Resource->SetPrivateData(WKPDID_D3DDebugObjectName, debugName.size(), debugName.data()));
    }

    uint64_t GetUsedVideoMemory() override
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
        m_DXGIAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo);
        return localMemoryInfo.CurrentUsage;
    }

    bool m_bTearingSupported = false;

    ComPtr<ID3D12CommandQueue> m_ComputeQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12Device> m_D3DDevice;
    ComPtr<ID3D12Resource> m_SwapChainD3D12Resources[2];
    ComPtr<IDXGIAdapter1> m_DXGIAdapter;
    ComPtr<IDXGIAdapter3> m_DXGIAdapter3;
    ComPtr<IDXGIFactory6> m_DXGIFactory;
    ComPtr<IDXGISwapChain3> m_SwapChain;
};

inline thread_local VkResult tl_VkResult;
#define VK_CHECK(call)                     \
    do                                     \
    {                                      \
        tl_VkResult = (call);              \
        assert(tl_VkResult == VK_SUCCESS); \
    } while (0)

static VkBool32 VKAPI_CALL VulkanDebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	// This silences warnings like "For optimal performance image layout should be VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL instead of GENERAL."
	// We'll assume other performance warnings are also not useful.
	if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
		return VK_FALSE;

	const char* type =
	    (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	        ? "ERROR"
	    : (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	        ? "WARNING"
	        : "INFO";

    std::string message = type;
    message += ": ";
    message += pMessage;

    LOG_DEBUG("[Vulkan]: %s", message.c_str());
    assert(false);

	return VK_FALSE;
}

class VulkanRHI : public GraphicRHI
{
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT m_DebugReportCallback = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

public:
    ~VulkanRHI()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);
        }

        if (m_DebugReportCallback)
        {
            vkDestroyDebugReportCallbackEXT(m_Instance, m_DebugReportCallback, 0);
        }

        if (m_Instance)
        {
            vkDestroyInstance(m_Instance, nullptr);
        }
    }

    nvrhi::DeviceHandle CreateDevice() override
    {
        VK_CHECK(volkInitialize());

        const bool bEnableValidation = g_CVarEnableGraphicRHIValidation.Get();

        std::vector<const char*> enabledLayers;
        if (bEnableValidation)
        {
            enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
        }

        std::vector<const char*> enabledExtensions{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
        if (bEnableValidation)
        {
            enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
        instanceInfo.ppEnabledLayerNames = enabledLayers.data();
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        instanceInfo.ppEnabledExtensionNames = enabledExtensions.data();

        VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &m_Instance));
        volkLoadInstanceOnly(m_Instance);

        if (bEnableValidation)
        {
            VkDebugReportCallbackCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
            createInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
            createInfo.pfnCallback = VulkanDebugReportCallback;

            VkDebugReportCallbackEXT callback = 0;
            VK_CHECK(vkCreateDebugReportCallbackEXT(m_Instance, &createInfo, 0, &callback));
            m_DebugReportCallback = callback;
        }

        nvrhi::vulkan::DeviceDesc deviceDesc;
        deviceDesc.errorCB = &g_NVRHIErrorCB;
        deviceDesc.instance = m_Instance;
        deviceDesc.physicalDevice;
        deviceDesc.device;

        // nvrhi::DeviceHandle device = nvrhi::vulkan::createDevice(deviceDesc);
        // if (bEnableValidation)
        // {
        //     device = nvrhi::validation::createValidationLayer(device);
        // }

        // return device;
        return {};
    }

    void InitSwapChainTextureHandles() override { assert(false && "Not Implemented!"); }
    uint32_t GetCurrentBackBufferIndex() override { assert(false && "Not Implemented!"); return UINT32_MAX; }
    void SwapChainPresent() override { assert(false && "Not Implemented!"); }
    void* GetNativeCommandList(nvrhi::CommandListHandle commandList) override { assert(false && "Not Implemented!"); return nullptr; }
    uint32_t GetTiledResourceSizeInBytes() override { assert(false && "Not Implemented!"); return 0; }
    uint32_t GetMaxTextureDimension() override { assert(false && "Not Implemented!"); return 0; }
    uint32_t GetMaxNumTextureMips() override { assert(false && "Not Implemented!"); return 0; }
    uint32_t GetMaxThreadGroupsPerDimension() override { assert(false && "Not Implemented!"); return 0; }
    uint64_t GetUsedVideoMemory() override { assert(false && "Not Implemented!"); return 0; }

    void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) override { assert(false && "Not Implemented!"); }
    void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) override { assert(false && "Not Implemented!"); }
};

GraphicRHI* GraphicRHI::Create()
{
    return g_CVarUseVulkanRHI.Get() ? static_cast<GraphicRHI*>(new VulkanRHI) : static_cast<GraphicRHI*>(new D3D12RHI);
}
