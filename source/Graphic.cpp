#include "Graphic.h"

#include "extern/debug_draw/debug_draw.hpp"
#include "extern/ktx_transcoder/basisu_transcoder.h"
#include "extern/ShaderMake/src/argparse.h"
#include "nvrhi/d3d12.h"
#include "nvrhi/validation.h"
#include "ShaderMake/ShaderBlob.h"

#include "CommonResources.h"
#include "DescriptorTableManager.h"
#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "SmallVector.h"
#include "Utilities.h"

#include "shaders/shared/MaterialData.h"
#include "shaders/shared/MeshData.h"

CommandLineOption<bool> g_EnableD3DDebug{ "d3ddebug", false };
CommandLineOption<bool> g_EnableGPUValidation{ "enablegpuvalidation", false };
CommandLineOption<bool> g_EnableGPUStablePowerState{ "enablegpustablepowerstate", false };

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

        // enforce requirment of 12_0 feature level at least
        static const D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0;

        auto CreateDevice = [this](D3D_FEATURE_LEVEL requestedFeatureLevel)
            {
                HRESULT_CALL(D3D12CreateDevice(g_DXGIAdapter, requestedFeatureLevel, IID_PPV_ARGS(&m_D3DDevice)));
                assert(m_D3DDevice);

                HRESULT_CALL(m_DeviceFeatures.Init(m_D3DDevice.Get()));
            };

        CreateDevice(kMinimumFeatureLevel);

        // use higher feature level if available
        if (m_DeviceFeatures.MaxSupportedFeatureLevel() != kMinimumFeatureLevel)
        {
            CreateDevice(m_DeviceFeatures.MaxSupportedFeatureLevel());
        }
        
        LOG_DEBUG("Initialized D3D12 Device with feature level: 0x%X", m_DeviceFeatures.MaxSupportedFeatureLevel());

        // disables 'dynamic frequency scaling' on GPU for reliable profiling
        if (g_EnableGPUStablePowerState.Get())
        {
            // Set stable power state if requested (only works when Windows Developer Mode is enabled)
            assert(g_Engine.m_bDeveloperModeEnabled);

            m_D3DDevice->SetStablePowerState(true);
        }
    }

    auto CreateQueue = [this](nvrhi::CommandQueue queue)
    {
        const char* queueName = nvrhi::utils::CommandQueueToString(queue);

        PROFILE_SCOPED(StringFormat("Create Queue [%s]", queueName));

        static const D3D12_COMMAND_LIST_TYPE kD3D12QueueTypes[] = { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY };

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = kD3D12QueueTypes[(uint32_t)queue];
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0; // For single-adapter, set to 0. Else, set a bit to identify the node

        ComPtr<ID3D12CommandQueue> outQueue;
        HRESULT_CALL(m_D3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&outQueue)));

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

            m_GPUThreadLogs.resize(g_Engine.m_Executor->num_workers() + 1); // +1 because main thread is index 0
            for (MicroProfileThreadLogGpu*& log : m_GPUThreadLogs)
            {
                log = MicroProfileThreadLogGpuAlloc();
            }
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

    ComPtr<IDXGISwapChain1> SwapChain;
    HRESULT_CALL(m_DXGIFactory->CreateSwapChainForHwnd(m_GraphicsQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
        g_Engine.m_WindowHandle,
        &SwapChainDesc,
        nullptr,
        nullptr,
        &SwapChain));

    // Disable Alt-Enter and other DXGI trickery...
    HRESULT_CALL(m_DXGIFactory->MakeWindowAssociation(g_Engine.m_WindowHandle, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER));

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

    tf::Taskflow tf;

    struct ShaderToStore
    {
        size_t m_Hash;
        nvrhi::ShaderHandle m_ShaderHandle;
    };
    std::vector<ShaderToStore> allShaders;
    std::mutex allShadersLck;

    std::stringstream stringStream{ fileFullText };
    std::string shaderEntryLine;
    while (std::getline(stringStream, shaderEntryLine))
    {
        tf.emplace([this, &allShaders, &allShadersLck, shaderEntryLine]
            {
                PROFILE_SCOPED("Init Shader");

                // Tokenize for argparse to read
                std::vector<const char*> configLineTokens;
                TokenizeLine((char*)shaderEntryLine.c_str(), configLineTokens);

                char* profile;
                char* entryPoint;
                char* unused;

                // use argparse to retrieve shader type & entry point, so we can reconstruct bin file name
                argparse_option options[] = {
                    OPT_STRING('T', "profile", &profile, "", nullptr, 0, 0),
                    OPT_STRING('E', "entryPoint", &entryPoint, "", nullptr, 0, 0),
                    OPT_STRING('D', "define", &unused, "", nullptr, 0, 0),
                    OPT_END(),
                };

                argparse argparse;
                argparse_init(&argparse, options, nullptr, 0);
                argparse_parse(&argparse, (int32_t)configLineTokens.size(), configLineTokens.data());

                // reconstruct bin file name
                // NOTE: after tokenization the line string is the 1st token of the line, which is the file name
                std::string binFullPath = (std::filesystem::path{ GetExecutableDirectory() } / "shaders" / "").string();
                binFullPath += std::filesystem::path{ shaderEntryLine }.stem().string() + "_" + entryPoint + ".bin";

                const std::string binFileName = std::filesystem::path{ binFullPath }.stem().string();

                std::vector<std::byte> shaderBlob;
                {
                    PROFILE_SCOPED("Read Shader bin");
                    ReadDataFromFile(binFullPath, shaderBlob);
                }
                assert(!shaderBlob.empty());

                std::vector<std::string> permutationDefines;
                ShaderMake::EnumeratePermutationsInBlob(shaderBlob.data(), shaderBlob.size(), permutationDefines);

                auto InitShader = [&](const void* pBinary, uint32_t binarySize, std::string_view shaderDebugName)
                    {
                        PROFILE_SCOPED("Init Shader");                        

                        nvrhi::ShaderDesc shaderDesc;
                        shaderDesc.debugName = shaderDebugName;

                        // kinda manual... but it's robust enough
                        std::string profileStr = profile;
                        StringUtils::ToLower(profileStr);
                        if (profileStr == "vs")
                            shaderDesc.shaderType = nvrhi::ShaderType::Vertex;
                        else if (profileStr == "ps")
                            shaderDesc.shaderType = nvrhi::ShaderType::Pixel;
                        else if (profileStr == "cs")
                            shaderDesc.shaderType = nvrhi::ShaderType::Compute;
                        else
                        {
                            assert(0);
                        }

                        size_t shaderHash = std::hash<std::string_view>{}(shaderDebugName);
                        nvrhi::ShaderHandle newShader = m_NVRHIDevice->createShader(shaderDesc, pBinary, binarySize);
                        assert(newShader);

                        {
                            AUTO_LOCK(allShadersLck);
                            allShaders.push_back({ shaderHash, newShader });
                        }

                        LOG_DEBUG("Init %s Shader: %s", nvrhi::utils::ShaderStageToString(shaderDesc.shaderType), shaderDebugName.data());
                    };

                // no permutations
                if (permutationDefines.empty())
                {
                    InitShader(shaderBlob.data(), (uint32_t)shaderBlob.size(), binFileName);
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


                        InitShader(pBinary, (uint32_t)binarySize, StringFormat("%s %s", binFileName.c_str(), definesStringCopy.c_str()));
                    }
                }
            });
    }

    g_Engine.m_Executor->corun(tf);

    for (const ShaderToStore& elem : allShaders)
    {
        assert(!m_AllShaders.contains(elem.m_Hash));
        m_AllShaders[elem.m_Hash] = elem.m_ShaderHandle;
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

void Graphic::InitVirtualBuffers()
{
    PROFILE_FUNCTION();

    // byte size amounts are sufficient for Sponza gltf

    nvrhi::BufferDesc desc;
    desc.byteSize = MB_TO_BYTES(16);
    desc.structStride = sizeof(RawVertexFormat);
    desc.debugName = "Virtual Vertex Buffer";
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    m_VirtualVertexBuffer.m_Buffer = m_NVRHIDevice->createBuffer(desc);

    desc.byteSize = MB_TO_BYTES(4);
    desc.debugName = "Virtual Index Buffer";
    desc.format = kIndexBufferFormat;
    desc.isVertexBuffer = false;
    desc.isIndexBuffer = true;
    desc.initialState = nvrhi::ResourceStates::IndexBuffer;
    m_VirtualIndexBuffer.m_Buffer = m_NVRHIDevice->createBuffer(desc);

    desc.byteSize = KB_TO_BYTES(8);
    desc.structStride = sizeof(MeshData);
    desc.debugName = "Mesh Data Buffer";
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    m_VirtualMeshDataBuffer.m_Buffer = m_NVRHIDevice->createBuffer(desc);

    desc.byteSize = KB_TO_BYTES(1);
    desc.structStride = sizeof(MaterialData);
    desc.debugName = "Material Data Buffer";
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    m_VirtualMaterialDataBuffer.m_Buffer = m_NVRHIDevice->createBuffer(desc);
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

static void HashBindingLayout(size_t& psoHash, const nvrhi::BindingLayoutVector& bindingLayouts)
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

nvrhi::GraphicsPipelineHandle Graphic::GetOrCreatePSO(const nvrhi::GraphicsPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer)
{
    size_t psoHash = 0;
    
    // hash primtive type
    HashCombine(psoHash, psoDesc.primType);

    // hash input layout
    if (nvrhi::InputLayoutHandle inputLayout = psoDesc.inputLayout)
    {
        for (uint32_t i = 0; i < inputLayout->getNumAttributes(); ++i)
        {
            const nvrhi::VertexAttributeDesc* desc = inputLayout->getAttributeDesc(i);

            // simply hash only each vertex format for now. others are not so important to be unique enough
            HashCombine(psoHash, desc->format);
        }
    }

    // hash VS & PS. just hash its debug name... assume all Shaders have unique debug names
    HashCombine(psoHash, psoDesc.VS->getDesc().debugName);
    if (psoDesc.PS)
    {
        HashCombine(psoHash, psoDesc.PS->getDesc().debugName);
    }

    // hash render state
    HashCombine(psoHash, HashRawMem(psoDesc.renderState));

    // hash binding layout
    HashBindingLayout(psoHash, psoDesc.bindingLayouts);
    
    // hash frame buffer
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

uint32_t Graphic::AppendOrRetrieveMaterialDataIndex(const MaterialData& materialData)
{
    const size_t materialDataHash = HashRawMem(materialData);

    static std::mutex s_CachedMaterialDataLock;
    AUTO_LOCK(s_CachedMaterialDataLock);

    if (!m_CachedMaterialDataIndices.contains(materialDataHash))
    {
        uint64_t byteOffset = m_VirtualMaterialDataBuffer.QueueAppend(&materialData, sizeof(MaterialData));
        m_CachedMaterialDataIndices[materialDataHash] = (uint32_t)byteOffset / sizeof(MaterialData);
    }
    return m_CachedMaterialDataIndices.at(materialDataHash);
}

uint32_t Graphic::GetOrCreateMesh(size_t hash, bool& bRetrievedFromCache)
{
    Mesh* ret = nullptr;
    bRetrievedFromCache = true;

    AUTO_LOCK(m_MeshCacheLock);

    auto it = m_MeshCache.find(hash);
    if (it == m_MeshCache.end())
    {
        ret = m_MeshPool.NewObject();
        assert(ret);

        m_MeshCache[hash] = ret;
        bRetrievedFromCache = false;

        ret->m_Idx = m_Meshes.size();
		m_Meshes.push_back(ret);
    }
    else
    {
        ret = it->second;
    }

	return ret->m_Idx;
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

    MicroProfileGpuBegin(D3D12CommandList, m_GPUThreadLogs.at(Engine::GetThreadID()));
}

void Graphic::EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist)
{
    PROFILE_FUNCTION();

    const uint64_t GPULog = MicroProfileGpuEnd(m_GPUThreadLogs.at(Engine::GetThreadID()));
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

    extern CommandLineOption<std::vector<int>> g_DisplayResolution;
    m_DisplayResolution.x = g_DisplayResolution.Get()[0];
    m_DisplayResolution.y = g_DisplayResolution.Get()[1];

    // TODO: upscaling stuff
    m_RenderResolution = m_DisplayResolution;

    // 1st phase Graphic init
    {
        tf::Taskflow tf;
        tf.emplace([this] { InitDevice(); });
        tf.emplace([this] { PROFILE_SCOPED("basisu_transcoder_init"); basist::basisu_transcoder_init(); });

        g_Engine.m_Executor->corun(tf);
    }

    // 2nd phase Graphic init
    {
        tf::Taskflow tf;
        tf.emplace([this] { InitSwapChain(); });
        tf.emplace([this] { InitShaders(); });
        tf::Task initDescriptorTable = tf.emplace([this] { InitDescriptorTable(); });
        tf::Task initCommonResources = tf.emplace([this] { m_CommonResources = std::make_shared<CommonResources>(); g_CommonResources.Initialize(); });
        tf.emplace([this] { m_Scene = std::make_shared<Scene>(); m_Scene->Initialize(); });
        tf.emplace([this] { InitVirtualBuffers(); });

        for (IRenderer* renderer : IRenderer::ms_AllRenderers)
        {
            tf.emplace([this, renderer]
                {
                    PROFILE_SCOPED(renderer->m_Name.c_str());
                    LOG_DEBUG("Init Renderer: %s", renderer->m_Name.c_str());
                    renderer->Initialize();
                });
        }

        initCommonResources.succeed(initDescriptorTable);

        // MT init & wait
        g_Engine.m_Executor->corun(tf);
    }

    // execute all cmd lists that was created & populated during init phase
    ExecuteAllCommandLists();
}

void Graphic::Shutdown()
{
    // wait for latest swap chain present to be done
    m_NVRHIDevice->waitForIdle();

    m_Scene->Shutdown();
    m_Scene.reset();

    m_TextureCache.clear();
    m_MeshCache.clear();
    m_AllShaders.clear();
    m_CachedGraphicPSOs.clear();
    m_CachedComputePSOs.clear();
    m_CachedBindingLayouts.clear();
    m_MeshPool.DeleteAll();

    // manually call destructor for all Renderers as they may hold resource handles
    for (IRenderer* renderer : IRenderer::ms_AllRenderers)
    {
        renderer->~IRenderer();
    }

    m_CommonResources.reset();

    for (uint32_t i = 0; i < (uint32_t)nvrhi::CommandQueue::Count; i++)
    {
        m_AllCommandLists[i].clear();
    }

    m_FrameTimerQuery.Reset();

    // Make sure that all frames have finished rendering & garbage collect
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

        m_NVRHIDevice->waitForIdle();
        m_NVRHIDevice->runGarbageCollection();

        m_CachedGraphicPSOs.clear();
        m_CachedComputePSOs.clear();

        InitShaders();

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

    // clear debug draw vertices' data if option to render debug primitives is disabled
    if (!g_GraphicPropertyGrid.m_DebugControllables.m_bRenderDebugDraw)
    {
        dd::clear();
    }

    m_VirtualVertexBuffer.CommitPendingUploads();
    m_VirtualIndexBuffer.CommitPendingUploads();
    m_VirtualMeshDataBuffer.CommitPendingUploads();
    m_VirtualMaterialDataBuffer.CommitPendingUploads();

    tf::Taskflow tf;

    // Releases the resources that were referenced in the command lists that have finished executing
    tf.emplace([this]
        {
            PROFILE_SCOPED("Graphics Garbage Collection");
            m_NVRHIDevice->runGarbageCollection();
        });

    tf.emplace([this] { m_Scene->Update(); });

    // MT execute all graphic update tasks
    g_Engine.m_Executor->run(tf).wait();

    m_Scene->PostRender();

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
    HRESULT_CALL(m_SwapChain->Present(kSyncInterval, kFlags));
}

uint32_t Graphic::GetThreadID()
{
    return Engine::GetThreadID();
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
            m_NVRHIDevice->waitForIdle();
        }

        m_NVRHIDevice->executeCommandLists(&m_PendingCommandLists[0], m_PendingCommandLists.size());
        m_PendingCommandLists.clear();
    }

    for (MicroProfileThreadLogGpu*& log : m_GPUThreadLogs)
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
    nvrhi::GraphicsPipelineDesc PSODesc;
    PSODesc.VS = g_Graphic.GetShader("fullscreen_VS_FullScreenTriangle");
    PSODesc.PS = g_Graphic.GetShader(pixelShaderName);
    PSODesc.renderState = nvrhi::RenderState{ nvrhi::BlendState{ blendState }, depthStencilState, g_CommonResources.CullNone };
    PSODesc.bindingLayouts = { bindingLayout };

    nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

    const nvrhi::TextureDesc& renderTargetDesc = frameBufferDesc.colorAttachments.at(0).texture->getDesc();

    const nvrhi::Viewport& viewPort = viewPortIn ? *viewPortIn : nvrhi::Viewport{ (float)renderTargetDesc.width, (float)renderTargetDesc.height };

    nvrhi::GraphicsState drawState;
    drawState.framebuffer = frameBuffer;
    drawState.viewport.addViewportAndScissorRect(viewPort);
    drawState.bindings = { bindingSet };
    drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);

    commandList->setGraphicsState(drawState);

    if (pushConstantsData)
	{
		commandList->setPushConstants(pushConstantsData, pushConstantsBytes);
	}

    nvrhi::DrawArguments drawArguments;
    drawArguments.vertexCount = 3;

    commandList->draw(drawArguments);
}

void Graphic::AddComputePass(
    nvrhi::CommandListHandle commandList,
    std::string_view shaderName,
    const nvrhi::BindingSetDesc& bindingSetDesc,
    const Vector3U& dispatchGroupSize,
    nvrhi::BufferHandle indirectArgsBuffer,
    uint32_t indirectArgsBufferOffsetBytes,
    nvrhi::BufferHandle indirectCountBuffer,
    uint32_t indirectCountBufferOffsetBytes,
    const void* pushConstantsData,
    size_t pushConstantsBytes)
{
    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(commandList, shaderName.data());

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.CS = GetShader(shaderName);
    pipelineDesc.bindingLayouts = { bindingLayout };

    nvrhi::ComputeState computeState;
    computeState.bindings = { bindingSet };
    computeState.pipeline = GetOrCreatePSO(pipelineDesc);

    if (indirectArgsBuffer)
    {
        assert(dispatchGroupSize.x == 0 && dispatchGroupSize.y == 0 && dispatchGroupSize.z == 0); // indirect dispatch does not need group size
        computeState.indirectParams = indirectArgsBuffer;

        if (indirectCountBuffer)
        {
            computeState.indirectCountBuffer = indirectCountBuffer;
        }
    }

    commandList->setComputeState(computeState);

    if (pushConstantsData)
    {
        commandList->setPushConstants(pushConstantsData, pushConstantsBytes);
    }

    if (indirectArgsBuffer)
    {
        commandList->dispatchIndirect(indirectArgsBufferOffsetBytes, indirectCountBufferOffsetBytes);
    }
    else
    {
        commandList->dispatch(dispatchGroupSize.x, dispatchGroupSize.y, dispatchGroupSize.z);
    }
}

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
