#include "Graphic.h"

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/nvrhi/include/nvrhi/d3d12.h"
#include "extern/nvrhi/include/nvrhi/validation.h"
#include "extern/shadermake/include/ShaderMake/ShaderBlob.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_properties.h"

#if NVRHI_WITH_AFTERMATH
#include "nvrhi/common/aftermath.h"
#endif

#include "CommonResources.h"
#include "DescriptorTableManager.h"
#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "SmallVector.h"
#include "Utilities.h"

CommandLineOption<bool> g_EnableD3DDebug{ "d3ddebug", false };
CommandLineOption<bool> g_EnableGPUValidation{ "enablegpuvalidation", false };

PRAGMA_OPTIMIZE_OFF;
void DeviceRemovedHandler()
{
    LOG_DEBUG("Device removed!");
    
    // TODO:
    //D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 DredAutoBreadcrumbsOutput{};
    //D3D12_DRED_PAGE_FAULT_OUTPUT1 DredPageFaultOutput{};
    //HRESULT_CALL(pDred->GetAutoBreadcrumbsOutput1(&DredAutoBreadcrumbsOutput));
    //HRESULT_CALL(pDred->GetPageFaultAllocationOutput1(&DredPageFaultOutput));

    assert(0);
}

// NVRHI message CB
class NVRHIMessageCallback : public nvrhi::IMessageCallback
{
    // Inherited via IMessageCallback
    void message(nvrhi::MessageSeverity severity, const char* messageText) override
    {
        switch (severity)
        {
            // just print info messages
        case nvrhi::MessageSeverity::Info:
            LOG_DEBUG("[NVRHI]: %s", messageText);
            break;

            // treat everything else critically
        case nvrhi::MessageSeverity::Warning:
        case nvrhi::MessageSeverity::Error:
        case nvrhi::MessageSeverity::Fatal:
            LOG_DEBUG("[NVRHI]: %s", messageText);
            assert(false);
            break;
        }
    }
};
static NVRHIMessageCallback g_NVRHIErrorCB;
PRAGMA_OPTIMIZE_ON;

IDXGIAdapter1* g_DXGIAdapter;

// copied from d3dx12.h
static D3D_FEATURE_LEVEL QueryHighestFeatureLevel(ID3D12Device* device)
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
}

void Graphic::InitDevice()
{
    {
        PROFILE_SCOPED("CreateDXGIFactory");

        const UINT factoryFlags = g_EnableD3DDebug.Get() ? DXGI_CREATE_FACTORY_DEBUG : 0;
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
    if (g_EnableD3DDebug.Get())
    {
        // enable DRED
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
        HRESULT_CALL(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

        // Turn on auto-breadcrumbs and page fault reporting.
        pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

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

    #if NVRHI_WITH_AFTERMATH
        m_AftermathCrashDumper.EnableCrashDumpTracking();
    #endif

        // enforce requirment of 12_0 feature level at least
        static const D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0;
        HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, kMinimumFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));

        const D3D_FEATURE_LEVEL maxSupportedFeatureLevel = QueryHighestFeatureLevel(m_D3DDevice.Get());

        // use higher feature level if available
        if (maxSupportedFeatureLevel != kMinimumFeatureLevel)
        {
            HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, maxSupportedFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));
        }
        
        LOG_DEBUG("Initialized D3D12 Device with feature level: 0x%X", maxSupportedFeatureLevel);
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

        m_GPUQueueLogs[(uint32_t)queue] = MicroProfileInitGpuQueue(queueName);

        return outQueue;
    };

    // MT create queues
    tf::Taskflow tf;
    tf.emplace([&]() { m_GraphicsQueue = CreateQueue(nvrhi::CommandQueue::Graphics); });
    //tf.emplace([&]() { m_ComputeQueue = CreateQueue(nvrhi::CommandQueue::Compute); });
    //tf.emplace([&]() { m_CopyQueue = CreateQueue(nvrhi::CommandQueue::Copy); });
    g_Engine.m_Executor->corun(tf);

    // clear Taskflow... we'll be using it again for more MT init in this func
    tf.clear();

    tf.emplace([this]()
        {
            PROFILE_SCOPED("Init GPU Profiler");

            void* pCommandQueues[] = { m_GraphicsQueue.Get() };
            MicroProfileGpuInitD3D12(m_D3DDevice.Get(), 1, pCommandQueues);
            MicroProfileSetCurrentNodeD3D12(0);

            m_GPUThreadLogs.reserve(g_Engine.m_Executor->num_workers() + 1); // +1 because main thread is index 0
        });

    tf.emplace([this]()
        {
            PROFILE_SCOPED("nvrhi::createDevice");

            // create NVRHI device handle
            nvrhi::d3d12::DeviceDesc deviceDesc;
            deviceDesc.errorCB = &g_NVRHIErrorCB;
            deviceDesc.pDevice = m_D3DDevice.Get();
            deviceDesc.pGraphicsCommandQueue = m_GraphicsQueue.Get();
            deviceDesc.pComputeCommandQueue = m_ComputeQueue.Get();
            deviceDesc.pCopyCommandQueue = m_CopyQueue.Get();

            m_NVRHIDevice = nvrhi::d3d12::createDevice(deviceDesc);

            for (constexpr auto kFeatures = magic_enum::enum_entries<nvrhi::Feature>();
                const auto& feature : kFeatures)
            {
                const bool bFeatureSupported = m_NVRHIDevice->queryFeatureSupport(feature.first);
                LOG_DEBUG("Feature Support for [%s]: [%d]", feature.second.data(), bFeatureSupported);

                auto EnsureFeatureSupport = [&](nvrhi::Feature featureRequested)
                    {
                        if (feature.first == featureRequested)
                        {
                            assert(bFeatureSupported);
                        }
                    };

                EnsureFeatureSupport(nvrhi::Feature::Meshlets);
                EnsureFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);
                EnsureFeatureSupport(nvrhi::Feature::RayTracingPipeline);
                EnsureFeatureSupport(nvrhi::Feature::RayQuery);
            }

            m_FrameTimerQuery = m_NVRHIDevice->createTimerQuery();

            if (g_EnableD3DDebug.Get())
            {
                m_NVRHIDevice = nvrhi::validation::createValidationLayer(m_NVRHIDevice); // make the rest of the application go through the validation layer

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

        #if NVRHI_WITH_AFTERMATH
            m_NVRHIDevice->getAftermathCrashDumpHelper().registerShaderBinaryLookupCallback(this, std::bind(&Graphic::FindShaderFromHashForAftermath, this, std::placeholders::_1, std::placeholders::_2));
        #endif
        });

    // MT init GPU Profiler & nvrhi device
    g_Engine.m_Executor->corun(tf);
}

void Graphic::InitSwapChain()
{
    PROFILE_FUNCTION();

    BOOL tearingSupported{};
    HRESULT_CALL(m_DXGIFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported)));
    m_bTearingSupported = tearingSupported;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
    SwapChainDesc.Width = m_DisplayResolution.x;
    SwapChainDesc.Height = m_DisplayResolution.y;
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
        textureDesc.width = m_DisplayResolution.x;
        textureDesc.height = m_DisplayResolution.y;
        textureDesc.format = nvrhi::Format::RGBA8_UNORM;
        textureDesc.debugName = "SwapChainBuffer";
        textureDesc.isRenderTarget = true;
        textureDesc.initialState = nvrhi::ResourceStates::Present;

        m_SwapChainTextureHandles[i] = m_NVRHIDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object{ m_SwapChainD3D12Resources[i].Get() }, textureDesc);
    }
}

void Graphic::InitShaders()
{
    PROFILE_FUNCTION();

    m_AllShaders.clear();

    std::filesystem::path inputPath = std::filesystem::path{ GetExecutableDirectory() }.parent_path() / "source" / "shaders" / "shaderstocompile.txt";
    std::string fileFullText;
    ReadTextFromFile(inputPath.string(), fileFullText);

    std::stringstream stringStream{ fileFullText };
    std::string shaderEntryLine;
    while (std::getline(stringStream, shaderEntryLine))
    {
        if (shaderEntryLine.empty())
        {
            continue;
        }

        PROFILE_SCOPED("Process Shader Line");

        // Tokenize for argparse to read
        std::vector<const char*> configLineTokens;
        TokenizeLine((char*)shaderEntryLine.c_str(), configLineTokens);

        // use cxxopts lib to conveniently retrieve shader type & entry point, so we can reconstruct bin file name
        cxxopts::Options options{ "Shader Line Parser", "" };
        options.allow_unrecognised_options();
        options.add_options()
            ("T", "profile", cxxopts::value<std::string>())
            ("E", "entryPoint", cxxopts::value<std::string>());

        const cxxopts::ParseResult parseResult = options.parse(configLineTokens.size(), configLineTokens.data());

        const std::string profile = parseResult["T"].as<std::string>();
        const std::string entryPoint = parseResult["E"].as<std::string>();

        // kinda manual... but it's robust enough
        // NOTE: this enum doesn't matter if we're not using NVRHI_D3D12_WITH_NVAPI
        nvrhi::ShaderType shaderType = nvrhi::ShaderType::None;
        std::string profileStr = profile;
        StringUtils::ToLower(profileStr);
        if (profileStr == "vs")
            shaderType = nvrhi::ShaderType::Vertex;
        else if (profileStr == "ps")
            shaderType = nvrhi::ShaderType::Pixel;
        else if (profileStr == "cs")
            shaderType = nvrhi::ShaderType::Compute;
        else if (profileStr == "ms")
            shaderType = nvrhi::ShaderType::Mesh;
        else if (profileStr == "as")
            shaderType = nvrhi::ShaderType::Amplification;
        else if (profileStr == "lib")
            shaderType = nvrhi::ShaderType::AllRayTracing; // ???

        assert(shaderType != nvrhi::ShaderType::None);

        // reconstruct bin file name
        // NOTE: after tokenization the line string is the 1st token of the line, which is the file name
        std::string binFullPath = (std::filesystem::path{ GetExecutableDirectory() } / "shaders" / "").string();

        // if the entry point is 'main', it won't be appended to the bin file name. Thanks ShaderMake. That's retarded.
        if (entryPoint == "main")
        {
            binFullPath += std::filesystem::path{ shaderEntryLine }.stem().string() + ".bin";
        }
        else
        {
            binFullPath += std::filesystem::path{ shaderEntryLine }.stem().string() + "_" + entryPoint + ".bin";
        }

        const std::string binFileName = std::filesystem::path{ binFullPath }.stem().string();

        std::vector<std::byte> shaderBlob;
        {
            PROFILE_SCOPED("Read Shader bin");
            ReadDataFromFile(binFullPath, shaderBlob);
        }
        assert(!shaderBlob.empty());

        std::vector<std::string> permutationDefines;
        ShaderMake::EnumeratePermutationsInBlob(shaderBlob.data(), shaderBlob.size(), permutationDefines);

        auto InitShaderHandle = [&, entryPoint, shaderType](const void* pBinary, uint32_t binarySize, std::string_view shaderDebugName)
            {
                PROFILE_SCOPED("Init Shader Handle");

                nvrhi::ShaderDesc shaderDesc;
                shaderDesc.shaderType = shaderType;
                shaderDesc.debugName = shaderDebugName;
                shaderDesc.entryName = entryPoint;

                size_t shaderHash = std::hash<std::string_view>{}(shaderDebugName);
                nvrhi::ShaderHandle newShader = m_NVRHIDevice->createShader(shaderDesc, pBinary, binarySize);
                assert(newShader);

                m_AllShaders[shaderHash] = newShader;

                LOG_DEBUG("Shader name: %s, Type: %s, Entry: %s", shaderDebugName.data(), nvrhi::utils::ShaderStageToString(shaderType), entryPoint.c_str());
            };

        // no permutations
        if (permutationDefines.empty())
        {
            InitShaderHandle(shaderBlob.data(), (uint32_t)shaderBlob.size(), binFileName);
        }

        // permutations. enumerate through all and init
        else
        {
            const uint32_t kNbMaxConstants = 8;

            // 'enumeratePermutationsInBlob' will return an array of strings of all combinations of permutation defines
            // assume each instance of '=' character represents a Shader #define
            const int64_t nbConstants = std::count(permutationDefines[0].begin(), permutationDefines[0].end(), '=');
            assert(nbConstants <= kNbMaxConstants);

            for (std::string permutationDefine : permutationDefines) // NOTE: deliberately not by const ref
            {
                // for debug name purposes
                const std::string definesStringCopy = permutationDefine;

                ShaderMake::ShaderConstant shaderConstants[kNbMaxConstants]{};

                std::vector<const char*> constantAndValueStrings;
                TokenizeLine((char*)permutationDefine.c_str(), constantAndValueStrings);

                uint32_t i = 0;
                for (const char* constantAndValueString : constantAndValueStrings)
                {
                    ShaderMake::ShaderConstant& shaderConstant = shaderConstants[i++];

                    shaderConstant.name = strtok((char*)constantAndValueString, "=");
                    shaderConstant.value = strtok(nullptr, "=");
                }

                const void* pBinary = nullptr;
                size_t binarySize = 0;
                if (!ShaderMake::FindPermutationInBlob(shaderBlob.data(), shaderBlob.size(), shaderConstants, (uint32_t)nbConstants, &pBinary, &binarySize))
                {
                    LOG_DEBUG("%s", ShaderMake::FormatShaderNotFoundMessage(shaderBlob.data(), shaderBlob.size(), shaderConstants, (uint32_t)nbConstants).c_str());
                    assert(false);
                }

                const std::string shaderDebugName = StringFormat("%s %s", binFileName.c_str(), definesStringCopy.c_str());
                InitShaderHandle(pBinary, (uint32_t)binarySize, shaderDebugName);
            }
        }
    }
}

void Graphic::InitDescriptorTable()
{
    PROFILE_FUNCTION();

    nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
    bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
    bindlessLayoutDesc.maxCapacity = kBindlessLayoutCapacity;
    bindlessLayoutDesc.registerSpaces = { nvrhi::BindingLayoutItem::Texture_SRV(1) };
    m_BindlessLayout = g_Graphic.GetOrCreateBindingLayout(bindlessLayoutDesc);

    m_DescriptorTableManager = std::make_shared<DescriptorTableManager>(m_NVRHIDevice, m_BindlessLayout);
}

nvrhi::ShaderHandle Graphic::GetShader(std::string_view shaderBinName)
{
    const size_t hash = std::hash<std::string_view>{}(shaderBinName);

    auto it = m_AllShaders.find(hash);
    assert(it != m_AllShaders.end()); // double-check Shader Bin Name

    return it->second;
}

static size_t HashBindingLayoutDesc(const nvrhi::BindingLayoutDesc& layoutDesc)
{
    size_t layoutHash = 0;
    for (const nvrhi::BindingLayoutItem& layoutItem : layoutDesc.bindings)
    {
        // just hash each layout item as a whole. it only contains PODs
        HashCombine(layoutHash, HashRawMem(layoutItem));
    }

    return layoutHash;
}

static size_t HashBindingLayoutDesc(const nvrhi::BindlessLayoutDesc& layoutDesc)
{
    size_t layoutHash = 0;
    for (const nvrhi::BindingLayoutItem& layoutItem : layoutDesc.registerSpaces)
    {
        // just hash each layout item as a whole. it only contains PODs
        HashCombine(layoutHash, HashRawMem(layoutItem));
    }

    return layoutHash;
}

nvrhi::BindingLayoutHandle Graphic::GetOrCreateBindingLayout(const nvrhi::BindingLayoutDesc& layoutDesc)
{
    PROFILE_FUNCTION();

    const size_t layoutHash = HashBindingLayoutDesc(layoutDesc);

    static std::mutex s_Lck;
    AUTO_LOCK(s_Lck);

    nvrhi::BindingLayoutHandle& bindingLayout = m_CachedBindingLayouts[layoutHash];
    if (!bindingLayout)
    {
        bindingLayout = m_NVRHIDevice->createBindingLayout(layoutDesc);
        //LOG_DEBUG("New Binding Layout: [%zx]", layoutHash);
    }
    return bindingLayout;
}

nvrhi::BindingLayoutHandle Graphic::GetOrCreateBindingLayout(const nvrhi::BindlessLayoutDesc& layoutDesc)
{
    const size_t layoutHash = HashBindingLayoutDesc(layoutDesc);

    static std::mutex s_Lck;
    AUTO_LOCK(s_Lck);

    nvrhi::BindingLayoutHandle& bindingLayout = m_CachedBindingLayouts[layoutHash];
    if (!bindingLayout)
    {
        bindingLayout = m_NVRHIDevice->createBindlessLayout(layoutDesc);
        //LOG_DEBUG("New Bindless Layout: [%zx]", layoutHash);
    }
    return bindingLayout;
}

static void HashBindingLayout(size_t& psoHash, std::span<const nvrhi::BindingLayoutHandle> bindingLayouts)
{
    for (nvrhi::BindingLayoutHandle bindingLayout : bindingLayouts)
    {
        if (const nvrhi::BindlessLayoutDesc* layoutDesc = bindingLayout->getBindlessDesc())
        {
            HashCombine(psoHash, HashBindingLayoutDesc(*layoutDesc));
        }

        if (const nvrhi::BindingLayoutDesc* layoutDesc = bindingLayout->getDesc())
        {
            HashCombine(psoHash, HashBindingLayoutDesc(*layoutDesc));
        }
    }
}
 
static void HashShaderHandle(size_t& hash, nvrhi::ShaderHandle shader) { if (shader) { HashCombine(hash, shader->getDesc().debugName); } };
static void HashBindingLayout(size_t& hash, nvrhi::BindingLayoutHandle layout) { if (layout) { HashBindingLayout(hash, std::array{ layout }); } };

static std::size_t HashCommonGraphicStates(
    nvrhi::PrimitiveType primType,
    nvrhi::ShaderHandle PS,
    const nvrhi::RenderState& renderState,
    const nvrhi::BindingLayoutVector& bindingLayouts,
    nvrhi::FramebufferHandle frameBuffer
)
{
    assert(PS->getDesc().shaderType == nvrhi::ShaderType::Pixel);

    size_t psoHash = 0;

    HashCombine(psoHash, primType);
    if (PS)
    {
        HashCombine(psoHash, PS->getDesc().debugName);
    }

    HashCombine(psoHash, HashRawMem(renderState));

    HashBindingLayout(psoHash, bindingLayouts);

    const nvrhi::FramebufferDesc& frameBufferDesc = frameBuffer->getDesc();
    for (const nvrhi::FramebufferAttachment& RT : frameBufferDesc.colorAttachments)
    {
        const nvrhi::TextureDesc& RTDesc = RT.texture->getDesc();
        HashCombine(psoHash, RTDesc.format);
    }

    if (frameBufferDesc.depthAttachment.valid())
    {
        const nvrhi::TextureDesc& DepthBufferDesc = frameBufferDesc.depthAttachment.texture->getDesc();
        HashCombine(psoHash, DepthBufferDesc.format);
    }

    return psoHash;
}

nvrhi::GraphicsPipelineHandle Graphic::GetOrCreatePSO(const nvrhi::GraphicsPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer)
{
    size_t psoHash = HashCommonGraphicStates(psoDesc.primType, psoDesc.PS, psoDesc.renderState, psoDesc.bindingLayouts, frameBuffer);
    
    if (nvrhi::InputLayoutHandle inputLayout = psoDesc.inputLayout)
    {
        for (uint32_t i = 0; i < inputLayout->getNumAttributes(); ++i)
        {
            const nvrhi::VertexAttributeDesc* desc = inputLayout->getAttributeDesc(i);

            // simply hash only each vertex format for now. others are not so important to be unique enough
            HashCombine(psoHash, desc->format);
        }
    }

    HashCombine(psoHash, psoDesc.VS->getDesc().debugName);

    static std::mutex s_CachedGraphicPSOsLock;
    AUTO_LOCK(s_CachedGraphicPSOsLock);

    nvrhi::GraphicsPipelineHandle& graphicsPipeline = m_CachedGraphicPSOs[psoHash];
    if (!graphicsPipeline)
    {
        PROFILE_SCOPED("createGraphicsPipeline");
        //LOG_DEBUG("New Graphic PSO: [%zx]", psoHash);
        graphicsPipeline = m_NVRHIDevice->createGraphicsPipeline(psoDesc, frameBuffer);
    }
    return graphicsPipeline;
}

nvrhi::MeshletPipelineHandle Graphic::GetOrCreatePSO(const nvrhi::MeshletPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer)
{
    size_t psoHash = HashCommonGraphicStates(psoDesc.primType, psoDesc.PS, psoDesc.renderState, psoDesc.bindingLayouts, frameBuffer);

	if (psoDesc.AS)
	{
		HashCombine(psoHash, psoDesc.AS->getDesc().debugName);
	}

	HashCombine(psoHash, psoDesc.MS->getDesc().debugName);

    static std::mutex s_CachedMeshletPSOsLock;
    AUTO_LOCK(s_CachedMeshletPSOsLock);

    nvrhi::MeshletPipelineHandle& pipeline = m_CachedMeshletPSOs[psoHash];
    if (!pipeline)
    {
        PROFILE_SCOPED("createMeshletPipeline");
        //LOG_DEBUG("New Meshlet PSO: [%zx]", psoHash);
        pipeline = m_NVRHIDevice->createMeshletPipeline(psoDesc, frameBuffer);
    }
    return pipeline;
}

nvrhi::ComputePipelineHandle Graphic::GetOrCreatePSO(const nvrhi::ComputePipelineDesc& psoDesc)
{
    size_t psoHash = 0;

    // hash CS. just hash its debug name... assume all Shaders have unique debug names
    HashCombine(psoHash, psoDesc.CS->getDesc().debugName);

    // hash binding layout
    HashBindingLayout(psoHash, psoDesc.bindingLayouts);

    static std::mutex s_CachedComputePSOsLock;
    AUTO_LOCK(s_CachedComputePSOsLock);

    nvrhi::ComputePipelineHandle& computePipeline = m_CachedComputePSOs[psoHash];
    if (!computePipeline)
    {
        PROFILE_SCOPED("createComputePipeline");
        //LOG_DEBUG("New Compute PSO: [%zx]", psoHash);
        computePipeline = m_NVRHIDevice->createComputePipeline(psoDesc);
    }
    return computePipeline;
}

nvrhi::rt::PipelineHandle Graphic::GetOrCreatePSO(const nvrhi::rt::PipelineDesc& psoDesc)
{
    size_t hash = 0;

    for (const nvrhi::rt::PipelineShaderDesc& shaderDesc : psoDesc.shaders)
    {
        HashCombine(hash, shaderDesc.exportName);
        HashShaderHandle(hash, shaderDesc.shader);
        HashBindingLayout(hash, shaderDesc.bindingLayout);
    }

    for (const nvrhi::rt::PipelineHitGroupDesc& hitGroupDesc : psoDesc.hitGroups)
    {
        HashCombine(hash, hitGroupDesc.exportName);
        HashShaderHandle(hash, hitGroupDesc.closestHitShader);
        HashShaderHandle(hash, hitGroupDesc.anyHitShader);
        HashShaderHandle(hash, hitGroupDesc.intersectionShader);
        HashBindingLayout(hash, hitGroupDesc.bindingLayout);
        HashCombine(hash, hitGroupDesc.isProceduralPrimitive);
    }

    HashBindingLayout(hash, psoDesc.globalBindingLayouts);
    HashCombine(hash, psoDesc.maxPayloadSize);
    HashCombine(hash, psoDesc.maxAttributeSize);
    HashCombine(hash, psoDesc.maxRecursionDepth);
    HashCombine(hash, psoDesc.hlslExtensionsUAV);

    static std::mutex s_CachedRTPSOsLock;
    AUTO_LOCK(s_CachedRTPSOsLock);

    nvrhi::rt::PipelineHandle& pipeline = m_CachedRTPSOs[hash];
    if (!pipeline)
    {
        PROFILE_SCOPED("createRTPipeline");
        //LOG_DEBUG("New RT PSO: [%zx]", hash);
        pipeline = m_NVRHIDevice->createRayTracingPipeline(psoDesc);
    }

    return pipeline;
}

void Graphic::CreateBindingSetAndLayout(const nvrhi::BindingSetDesc& bindingSetDesc, nvrhi::BindingSetHandle& outBindingSetHandle, nvrhi::BindingLayoutHandle& outLayoutHandle)
{
    PROFILE_FUNCTION();

    // copied from nvrhi::utils::CreateBindingSetAndLayout
    auto ConvertSetToLayout = [](const nvrhi::BindingSetItemArray& setDesc)
        {
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::All;
            for (const nvrhi::BindingSetItem& item : setDesc)
            {
                nvrhi::BindingLayoutItem layoutItem{};
                layoutItem.slot = item.slot;
                layoutItem.type = item.type;
                if (item.type == nvrhi::ResourceType::PushConstants)
                    layoutItem.size = uint32_t(item.range.byteSize);
                layoutDesc.bindings.push_back(layoutItem);
            }
            return layoutDesc;
        };

    nvrhi::BindingLayoutDesc layoutDesc = ConvertSetToLayout(bindingSetDesc.bindings);

    outLayoutHandle = GetOrCreateBindingLayout(layoutDesc);
    assert(outLayoutHandle);

    outBindingSetHandle = m_NVRHIDevice->createBindingSet(bindingSetDesc, outLayoutHandle);
    assert(outBindingSetHandle);
}

nvrhi::CommandListHandle Graphic::AllocateCommandList(nvrhi::CommandQueue queueType)
{
    PROFILE_FUNCTION();

    const uint32_t queueIdx = (uint32_t)queueType;

    nvrhi::CommandListHandle ret;

    {
        AUTO_LOCK(m_FreeCommandListsLock);

        if (m_FreeCommandLists[queueIdx].empty())
        {
            nvrhi::CommandListParameters params;
            params.enableImmediateExecution = false; // always enable parallel executions
            params.queueType = queueType;

            ret = g_Graphic.m_NVRHIDevice->createCommandList(params);

            m_AllCommandLists[queueIdx].push_back(ret);
        }
        else
        {
            // Reuse the "oldest" from the free list
            ret = m_FreeCommandLists[queueIdx].front();
            m_FreeCommandLists[queueIdx].pop_front();
        }
    }

    // auto free next frame
    g_Engine.AddCommand([ret, this] { FreeCommandList(ret); });

    return ret;
}

void Graphic::FreeCommandList(nvrhi::CommandListHandle cmdList)
{
    STATIC_MULTITHREAD_DETECTOR();

    // TODO: use fences to guard these command lists from being used again before the GPU is done with them if needed

    m_FreeCommandLists[(uint32_t)cmdList->getDesc().queueType].push_back(cmdList);
}

void Graphic::BeginCommandList(nvrhi::CommandListHandle cmdList, std::string_view name)
{
    PROFILE_FUNCTION();

    cmdList->open();

    ID3D12GraphicsCommandList* D3D12CommandList = cmdList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    HRESULT_CALL(D3D12CommandList->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.data()));

    MicroProfileThreadLogGpu*& gpuLog = m_GPUThreadLogs[std::this_thread::get_id()];

    // create gpu log on first use
    if (!gpuLog)
    {
		LOG_DEBUG("Init GPU Thread Log for Thread: %d", std::this_thread::get_id());
        gpuLog = MicroProfileThreadLogGpuAlloc();
    }

    MicroProfileGpuBegin(D3D12CommandList, gpuLog);
}

void Graphic::EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist)
{
    PROFILE_FUNCTION();

    const uint64_t GPULog = MicroProfileGpuEnd(m_GPUThreadLogs.at(std::this_thread::get_id()));
    cmdList->m_GPULog = GPULog;

    cmdList->close();

    if (bQueueCmdlist)
    {
        QueueCommandList(cmdList);
    }
}

void Graphic::Initialize()
{
    PROFILE_FUNCTION();

    m_DisplayResolution = g_Engine.m_WindowSize;

    // TODO: upscaling stuff
    m_RenderResolution = m_DisplayResolution;

    InitDevice();

    tf::Taskflow tf;
    tf.emplace([this] { InitSwapChain(); });
    tf.emplace([this] { InitShaders(); });
    tf::Task initDescriptorTable = tf.emplace([this] { InitDescriptorTable(); });
    tf::Task initCommonResources = tf.emplace([this] { m_CommonResources = std::make_shared<CommonResources>(); g_CommonResources.Initialize(); });
    tf.emplace([this] { m_Scene = std::make_shared<Scene>(); m_Scene->Initialize(); });

    for (IRenderer* renderer : IRenderer::ms_AllRenderers)
    {
        tf.emplace([this, renderer]
                   {
                       PROFILE_SCOPED(renderer->m_Name.c_str());
                       LOG_DEBUG("Init Renderer: %s", renderer->m_Name.c_str());
                       renderer->Initialize();
                   }).succeed(initCommonResources);
    }

    initCommonResources.succeed(initDescriptorTable);

    // MT init & wait
    g_Engine.m_Executor->corun(tf);

    // execute all cmd lists that was created & populated during init phase
    ExecuteAllCommandLists();
}

void Graphic::Shutdown()
{
    // wait for latest swap chain present to be done
    verify(m_NVRHIDevice->waitForIdle());

#if NVRHI_WITH_AFTERMATH
    m_NVRHIDevice->getAftermathCrashDumpHelper().unRegisterShaderBinaryLookupCallback(this);
#endif

    m_Scene->Shutdown();
    m_Scene.reset();

    m_AllShaders.clear();
    m_CachedGraphicPSOs.clear();
    m_CachedMeshletPSOs.clear();
    m_CachedComputePSOs.clear();
    m_CachedBindingLayouts.clear();

    // manually call destructor for all Renderers as they may hold resource handles
    for (IRenderer* renderer : IRenderer::ms_AllRenderers)
    {
        renderer->~IRenderer();
    }

    m_CommonResources.reset();

    for (uint32_t i = 0; i < (uint32_t)nvrhi::CommandQueue::Count; i++)
    {
        m_AllCommandLists[i].clear();
		m_FreeCommandLists[i].clear();
    }

    m_FrameTimerQuery.Reset();

    // Make sure that all frames have finished rendering & garbage collect
    verify(m_NVRHIDevice->waitForIdle());
    m_NVRHIDevice->runGarbageCollection();

    MicroProfileGpuShutdown();
}

void Graphic::Update()
{
    PROFILE_FUNCTION();

    if (m_bTriggerReloadShaders)
    {
        PROFILE_SCOPED("Reload Shaders");

        LOG_DEBUG("Reloading all Shaders...");

        verify(m_NVRHIDevice->waitForIdle());
        m_NVRHIDevice->runGarbageCollection();

        m_CachedGraphicPSOs.clear();
        m_CachedMeshletPSOs.clear();
        m_CachedComputePSOs.clear();
        m_CachedRTPSOs.clear();

        // run as a task due to the usage of "corun" in the InitShaders function
        tf::Taskflow tf;
        tf.emplace([this] { InitShaders(); });
        g_Engine.m_Executor->corun(tf);

        m_bTriggerReloadShaders = false;
    }

    // get GPU time for previous frame
    if (m_NVRHIDevice->pollTimerQuery(m_FrameTimerQuery))
    {
        const float prevGPUTimeSeconds = m_NVRHIDevice->getTimerQueryTime(m_FrameTimerQuery);
        g_Engine.m_GPUTimeMs = Timer::SecondsToMilliSeconds(prevGPUTimeSeconds);
        m_NVRHIDevice->resetTimerQuery(m_FrameTimerQuery);
    }

    ++m_FrameCounter;

    // execute all cmd lists that may have been potentially added as engine commands
    ExecuteAllCommandLists();

    {
        PROFILE_SCOPED("Begin Frame Timer Query");

        nvrhi::CommandListHandle commandList = AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Begin Frame Timer Query");

        commandList->beginTimerQuery(m_FrameTimerQuery);
    }

    tf::Taskflow tf;

    // Releases the resources that were referenced in the command lists that have finished executing
    tf.emplace([this]
        {
            PROFILE_SCOPED("Graphics Garbage Collection");
            m_NVRHIDevice->runGarbageCollection();
        });

    tf.emplace([this] { m_Scene->Update(); });

    // MT execute all graphic update tasks
    g_Engine.m_Executor->corun(tf);

    {
        PROFILE_SCOPED("End Frame Timer Query");

        nvrhi::CommandListHandle commandList = AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "End Frame Timer Query");

        commandList->endTimerQuery(m_FrameTimerQuery);
    }

    // execute all cmd lists for this frame
    ExecuteAllCommandLists();

    // finally, present swap chain
    Present();
}

void Graphic::Present()
{
    PROFILE_FUNCTION();

    const UINT kSyncInterval = 0; // 0: no vsync, 1: vsync

    // When using sync interval 0, it is recommended to always pass the tearing flag when it is supported.
    const UINT kFlags = (kSyncInterval == 0 && m_bTearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    // Present the frame.
    const HRESULT presentResult = m_SwapChain->Present(kSyncInterval, kFlags);

    if (FAILED(presentResult))
    {
        verify(m_NVRHIDevice->waitForIdle());

    #if NVRHI_WITH_AFTERMATH
        AftermathCrashDump::WaitForCrashDump();
    #endif

        DeviceRemovedHandler();
        assert(0);
    }
}

void Graphic::SetGPUStablePowerState(bool bEnable)
{
    if (!g_Engine.m_bWindowsDeveloperMode)
    {
        return;
    }

    m_D3DDevice->SetStablePowerState(bEnable);
}

void Graphic::UpdateResourceDebugName(nvrhi::IResource* resource, std::string_view debugName)
{
    assert(resource);

    ID3D12Resource* D3D12Resource = resource->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    HRESULT_CALL(D3D12Resource->SetPrivateData(WKPDID_D3DDebugObjectName, debugName.size(), debugName.data()));
}

void Graphic::ExecuteAllCommandLists()
{
    if (m_PendingCommandLists.size())
    {
        PROFILE_SCOPED("Execute CommandLists");

        // need to call 'MicroProfileGpuSubmit' in the same order as ExecuteCommandLists
        for (nvrhi::CommandListHandle cmdList : m_PendingCommandLists)
        {
            assert(cmdList);
            assert(cmdList->m_GPULog != ULLONG_MAX);
            MicroProfileGpuSubmit((uint32_t)nvrhi::CommandQueue::Graphics, cmdList->m_GPULog);

            cmdList->m_GPULog = ULLONG_MAX;
        }

        {
            PROFILE_SCOPED("Wait for previous GPU Frame");
            verify(m_NVRHIDevice->waitForIdle());
        }

        m_NVRHIDevice->executeCommandLists(&m_PendingCommandLists[0], m_PendingCommandLists.size());
        m_PendingCommandLists.clear();
    }

    for (const auto& [threadID, log] : m_GPUThreadLogs)
    {
        MicroProfileThreadLogGpuReset(log);
    }
}

void Graphic::AddFullScreenPass(
    nvrhi::CommandListHandle commandList,
    const nvrhi::FramebufferDesc& frameBufferDesc,
    const nvrhi::BindingSetDesc& bindingSetDesc,
    std::string_view pixelShaderName,
    const nvrhi::BlendState::RenderTarget* blendStateIn,
    const nvrhi::DepthStencilState* depthStencilStateIn,
    const nvrhi::Viewport* viewPortIn,
    const void* pushConstantsData,
    size_t pushConstantsBytes)
{
    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(commandList, pixelShaderName.data());

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

    const nvrhi::BlendState::RenderTarget& blendState = blendStateIn ? *blendStateIn : g_CommonResources.BlendOpaque;
    const nvrhi::DepthStencilState& depthStencilState = depthStencilStateIn ? *depthStencilStateIn : g_CommonResources.DepthNoneStencilNone;

    // PSO
    nvrhi::MeshletPipelineDesc PSODesc;
    PSODesc.MS = g_Graphic.GetShader("fullscreen_MS_FullScreenTriangle");
    PSODesc.PS = g_Graphic.GetShader(pixelShaderName);
    PSODesc.renderState = nvrhi::RenderState{ nvrhi::BlendState{ blendState }, depthStencilState, g_CommonResources.CullNone };
    PSODesc.bindingLayouts = { bindingLayout };

    nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

    const nvrhi::TextureDesc& renderTargetDesc = frameBufferDesc.colorAttachments.at(0).texture->getDesc();

    const nvrhi::Viewport& viewPort = viewPortIn ? *viewPortIn : nvrhi::Viewport{ (float)renderTargetDesc.width, (float)renderTargetDesc.height };

    nvrhi::MeshletState meshletState;
    meshletState.framebuffer = frameBuffer;
    meshletState.viewport.addViewportAndScissorRect(viewPort);
    meshletState.bindings = { bindingSet };
    meshletState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);

    commandList->setMeshletState(meshletState);

    if (pushConstantsData)
	{
		commandList->setPushConstants(pushConstantsData, pushConstantsBytes);
	}

    commandList->dispatchMesh(1, 1, 1);
}

void Graphic::AddComputePass(const ComputePassParams& computePassParams)
{
    assert(computePassParams.m_CommandList);
    assert(!computePassParams.m_ShaderName.empty());
    assert(!computePassParams.m_BindingSetDesc.bindings.empty());

    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(computePassParams.m_CommandList, computePassParams.m_ShaderName.data());

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    CreateBindingSetAndLayout(computePassParams.m_BindingSetDesc, bindingSet, bindingLayout);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.CS = GetShader(computePassParams.m_ShaderName);
    pipelineDesc.bindingLayouts = { bindingLayout };

    if (computePassParams.m_ShouldAddBindlessResources)
    {
        pipelineDesc.bindingLayouts.push_back(m_BindlessLayout);
    }

    nvrhi::ComputeState computeState;
    computeState.pipeline = GetOrCreatePSO(pipelineDesc);
    computeState.bindings = { bindingSet };

    if (computePassParams.m_ShouldAddBindlessResources)
    {
        computeState.bindings.push_back(m_DescriptorTableManager->GetDescriptorTable());
    }

    if (computePassParams.m_IndirectArgsBuffer)
    {
        // indirect dispatch does not need group size
        assert(computePassParams.m_DispatchGroupSize.x == 0 && computePassParams.m_DispatchGroupSize.y == 0 && computePassParams.m_DispatchGroupSize.z == 0);
        computeState.indirectParams = computePassParams.m_IndirectArgsBuffer;
    }

    computePassParams.m_CommandList->setComputeState(computeState);

    if (computePassParams.m_PushConstantsData)
    {
        assert(computePassParams.m_PushConstantsBytes > 0);
        computePassParams.m_CommandList->setPushConstants(computePassParams.m_PushConstantsData, computePassParams.m_PushConstantsBytes);
    }

    if (computePassParams.m_IndirectArgsBuffer)
    {
        computePassParams.m_CommandList->dispatchIndirect(computePassParams.m_IndirectArgsBufferOffsetBytes);
    }
    else
    {
        assert(computePassParams.m_DispatchGroupSize.x != 0 && computePassParams.m_DispatchGroupSize.y != 0 && computePassParams.m_DispatchGroupSize.z != 0);
        computePassParams.m_CommandList->dispatch(computePassParams.m_DispatchGroupSize.x, computePassParams.m_DispatchGroupSize.y, computePassParams.m_DispatchGroupSize.z);
    }
}

Vector2 Graphic::GetCurrentJitterOffset()
{
    auto VanDerCorput = [](size_t base, size_t index)
        {
            float ret = 0.0f;
            float denominator = (float)base;
            while (index > 0)
            {
                const size_t multiplier = index % base;
                ret += (float)multiplier / denominator;
                index /= base;
                denominator *= base;
            }
            return ret;
        };

    const uint32_t index = (m_FrameCounter % 16) + 1;
    return Vector2{ VanDerCorput(2, index), VanDerCorput(3, index) } - Vector2{ 0.5f, 0.5f };
}

#if NVRHI_WITH_AFTERMATH
std::pair<const void*, size_t> Graphic::FindShaderFromHashForAftermath(uint64_t hash, std::function<uint64_t(std::pair<const void*, size_t>, nvrhi::GraphicsAPI)> hashGenerator)
{
    for (const auto [shaderHandleHash, shaderHandle] : m_AllShaders)
    {
        // NOTE: shaderHandleHash is simply the hash of the Shader's file name, not byte code hash

        const void* pByteCode;
        size_t size;
        shaderHandle->getBytecode(&pByteCode, &size);

        uint64_t entryHash = hashGenerator(std::make_pair(pByteCode, size), nvrhi::GraphicsAPI::D3D12);
        if (entryHash == hash)
        {
            return std::make_pair(pByteCode, size);
        }
    }
    return std::make_pair(nullptr, 0);
}
#endif // NVRHI_WITH_AFTERMATH

uint32_t FencedReadbackBuffer::GetWriteIndex() { return g_Graphic.m_FrameCounter % kNbBuffers; }
uint32_t FencedReadbackBuffer::GetReadIndex() { return (g_Graphic.m_FrameCounter + 1) % kNbBuffers; }

void FencedReadbackBuffer::Initialize(nvrhi::DeviceHandle device, uint32_t bufferSize)
{
    m_BufferSize = bufferSize;

    nvrhi::BufferDesc desc;
    desc.byteSize = bufferSize;
    desc.structStride = sizeof(float);
    desc.debugName = "ReadbackBuffer";
    desc.initialState = nvrhi::ResourceStates::CopyDest;
    desc.cpuAccess = nvrhi::CpuAccessMode::Read;
    desc.debugName = "FencedReadbackBuffer";

    for (uint32_t i = 0; i < kNbBuffers; ++i)
    {
        m_Buffers[i] = device->createBuffer(desc);
        m_EventQueries[i] = device->createEventQuery();
    }
}

void FencedReadbackBuffer::CopyTo(
    nvrhi::DeviceHandle device,
    nvrhi::CommandListHandle commandList,
    nvrhi::BufferHandle bufferSource,
    nvrhi::CommandQueue queue)
{
    assert(m_BufferSize > 0);

    const uint32_t writeIndex = GetWriteIndex();

    commandList->copyBuffer(m_Buffers[writeIndex], 0, bufferSource, 0, m_BufferSize);

    device->resetEventQuery(m_EventQueries[writeIndex]);
    device->setEventQuery(m_EventQueries[writeIndex], queue);
}

void FencedReadbackBuffer::Read(nvrhi::DeviceHandle device, void* outPtr)
{
    assert(m_BufferSize > 0);

    const uint32_t readIndex = GetReadIndex();

    if (device->pollEventQuery(m_EventQueries[readIndex]))
    {
        void* mappedPtr = device->mapBuffer(m_Buffers[readIndex], nvrhi::CpuAccessMode::Read);
        memcpy(outPtr, mappedPtr, m_BufferSize);
        device->unmapBuffer(m_Buffers[readIndex]);
    }
}
