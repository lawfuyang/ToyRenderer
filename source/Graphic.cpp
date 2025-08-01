#include "Graphic.h"

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/shadermake/ShaderMake/ShaderBlob.h"

#include "SDL3/SDL.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Scene.h"
#include "TextureFeedbackManager.h"
#include "Utilities.h"

#include "shaders/ShaderInterop.h"

static_assert(GraphicConstants::kMaxNumMeshLODs == kMaxNumMeshLODs);
static_assert(GraphicConstants::kMaxThreadGroupsPerDimension == kMaxThreadGroupsPerDimension);
static_assert(kMeshletShaderThreadGroupSize >= kMaxMeshletTriangles);
static_assert(kMeshletShaderThreadGroupSize >= kMaxMeshletVertices);
static_assert(sizeof(DrawIndexedIndirectArguments) == sizeof(nvrhi::DrawIndexedIndirectArguments));
static_assert(sizeof(DrawIndirectArguments) == sizeof(nvrhi::DrawIndirectArguments));
static_assert(std::is_same_v<uint32_t, GraphicConstants::IndexBufferFormat_t>);

CommandLineOption<bool> g_AttachRenderDoc{ "attachrenderdoc", false };
CommandLineOption<bool> g_ExecuteAndWaitPerCommandList{ "executeandwaitpercommandlist", false };
CommandLineOption<bool> g_ExecutePerCommandList{ "executepercommandlist", false };
CommandLineOption<bool> g_DisableTextureStreaming{ "disabletextureStreaming", true }; // TODO: set to false once tile texture streaming is done

void Graphic::InitRenderDocAPI()
{
    PROFILE_FUNCTION();

    if (!g_AttachRenderDoc.Get())
    {
        return;
    }

    LOG_DEBUG("Initializing RenderDoc API");
    HMODULE mod = ::LoadLibraryA("renderdoc.dll");
    assert(mod);

    pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    const int result = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&m_RenderDocAPI);
    assert(result == 1);

    m_RenderDocAPI->SetCaptureFilePathTemplate((std::filesystem::path{ GetExecutableDirectory() } / "RenderDocCapture").string().c_str());
}

void Graphic::InitDevice()
{
    PROFILE_FUNCTION();

    m_GraphicRHI = std::unique_ptr<GraphicRHI>{ GraphicRHI::Create() };

    m_NVRHIDevice = m_GraphicRHI->CreateDevice();

    for (constexpr auto kFeatures = magic_enum::enum_entries<nvrhi::Feature>();
         const auto & feature : kFeatures)
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

        EnsureFeatureSupport(nvrhi::Feature::HeapDirectlyIndexed);
        EnsureFeatureSupport(nvrhi::Feature::Meshlets);
        EnsureFeatureSupport(nvrhi::Feature::RayQuery);
        EnsureFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);

        // NOTE: not supported in RenderDoc as of 1.39
        if (!m_RenderDocAPI)
        {
            EnsureFeatureSupport(nvrhi::Feature::SamplerFeedback);
        }
        else
        {
            g_Scene->m_bEnableTextureStreaming = false;
        }

        if (feature.first == nvrhi::Feature::WaveLaneCountMinMax)
        {
            nvrhi::WaveLaneCountMinMaxFeatureInfo waveLaneCountMinMaxInfo;
            verify(m_NVRHIDevice->queryFeatureSupport(feature.first, &waveLaneCountMinMaxInfo, sizeof(waveLaneCountMinMaxInfo)));

            // ensure threads per wave == 32
            assert(waveLaneCountMinMaxInfo.minWaveLaneCount == waveLaneCountMinMaxInfo.maxWaveLaneCount); // NOTE: wtf does it mean if this is not true?
            assert(kNumThreadsPerWave == waveLaneCountMinMaxInfo.minWaveLaneCount);

            LOG_DEBUG("Wave Lane Count: %d", waveLaneCountMinMaxInfo.minWaveLaneCount);
        }
    }

    for (uint32_t i = 0; i < 2; ++i)
    {
        m_FrameTimerQuery[i] = m_NVRHIDevice->createTimerQuery();
    }
}

void Graphic::InitShaders()
{
    PROFILE_FUNCTION();

    m_AllShaders.clear();

    std::filesystem::path inputPath = std::filesystem::path{ GetExecutableDirectory() }.parent_path() / "shaderstocompile.txt";
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

        // NOTE: for raytracing, only support inline ray query, so dont have to parse weird shader file extensions

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

void Graphic::InitDescriptorTables()
{
    PROFILE_FUNCTION();

    nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
    bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
    bindlessLayoutDesc.maxCapacity = GraphicConstants::kSrvUavCbvBindlessLayoutCapacity;
    bindlessLayoutDesc.layoutType = nvrhi::BindlessLayoutDesc::LayoutType::MutableSrvUavCbv;
    m_SrvUavCbvBindlessLayout = GetOrCreateBindingLayout(bindlessLayoutDesc);
    m_SrvUavCbvDescriptorTableManager = std::make_shared<DescriptorTableManager>(m_SrvUavCbvBindlessLayout);
}

nvrhi::TextureHandle Graphic::GetCurrentBackBuffer()
{
    return m_SwapChainTextureHandles[m_GraphicRHI->GetCurrentBackBufferIndex()];
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

    HashCombine(layoutHash, layoutDesc.layoutType);

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

    // for some reason, in Release builds, there's a PSO hash leak when hashing the entire RenderState struct, so we only hash the individual members
    // did not investigate why
    HashCombine(psoHash, std::hash<nvrhi::BlendState>()(renderState.blendState));
    HashCombine(psoHash, HashRawMem(renderState.depthStencilState));
    HashCombine(psoHash, HashRawMem(renderState.rasterState));

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

nvrhi::IDescriptorTable* Graphic::GetSrvUavCbvDescriptorTable()
{
    return m_SrvUavCbvDescriptorTableManager->GetDescriptorTable();
}

uint32_t Graphic::GetIndexInHeap(uint32_t indexInTable) const
{
    assert(indexInTable != UINT_MAX);
    const uint32_t indexInHeap = m_SrvUavCbvDescriptorTableManager->GetIndexInHeap(indexInTable);
    assert(indexInHeap != UINT_MAX);
    return indexInHeap;
}

void Graphic::CreateBindingSetAndLayout(const nvrhi::BindingSetDesc& bindingSetDesc, nvrhi::BindingSetHandle& outBindingSetHandle, nvrhi::BindingLayoutHandle& outLayoutHandle, uint32_t registerSpace)
{
    PROFILE_FUNCTION();

    // copied from nvrhi::utils::CreateBindingSetAndLayout
    auto ConvertSetToLayout = [](std::span<const nvrhi::BindingSetItem> setDesc)
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::All;
        for (const nvrhi::BindingSetItem &item : setDesc)
        {
            nvrhi::BindingLayoutItem layoutItem{};
            layoutItem.slot = item.slot;
            layoutItem.type = item.type;
            layoutItem.size = 1;
            if (item.type == nvrhi::ResourceType::PushConstants)
                layoutItem.size = uint32_t(item.range.byteSize);
            layoutDesc.bindings.push_back(layoutItem);
        }
        return layoutDesc;
    };

    nvrhi::BindingLayoutDesc layoutDesc = ConvertSetToLayout(bindingSetDesc.bindings);
    layoutDesc.registerSpace = registerSpace;

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

            ret = m_NVRHIDevice->createCommandList(params);

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

MicroProfileThreadLogGpu*& Graphic::GetGPULogForCurrentThread()
{
    thread_local MicroProfileThreadLogGpu* tl_GPULog = nullptr;
    return tl_GPULog;
}

void Graphic::BeginCommandList(nvrhi::CommandListHandle cmdList, std::string_view name)
{
    PROFILE_FUNCTION();

    cmdList->open();

    m_GraphicRHI->SetRHIObjectDebugName(cmdList, name);

    if (!GetGPULogForCurrentThread())
    {
        GetGPULogForCurrentThread() = MicroProfileThreadLogGpuAlloc();
    }

    MicroProfileGpuBegin(m_GraphicRHI->GetNativeCommandList(cmdList), GetGPULogForCurrentThread());
}

void Graphic::EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist, bool bImmediateExecute)
{
    PROFILE_FUNCTION();

    assert(!(bQueueCmdlist && bImmediateExecute)); // cannot queue & execute immediately at the same time

    assert(GetGPULogForCurrentThread());
    cmdList->m_GPULog = MicroProfileGpuEnd(GetGPULogForCurrentThread());

    cmdList->close();

    if (bQueueCmdlist)
    {
        QueueCommandList(cmdList);
    }

    if (bImmediateExecute)
    {
        m_NVRHIDevice->executeCommandList(cmdList);
    }
}

void Graphic::Initialize()
{
    PROFILE_FUNCTION();

    m_DisplayResolution = g_Engine.m_WindowSize;

    // TODO: upscaling stuff
    m_RenderResolution = m_DisplayResolution;

    m_CommonResources = std::make_shared<CommonResources>();
    m_TextureFeedbackManager = std::make_shared<TextureFeedbackManager>();

    m_Scene->m_bEnableTextureStreaming = !g_DisableTextureStreaming.Get();

    InitRenderDocAPI();
    InitDevice();

    tf::Taskflow tf;
    tf.emplace([this] { m_GraphicRHI->InitSwapChainTextureHandles(); });
    tf.emplace([this] { InitShaders(); });
    tf::Task initDescriptorTable = tf.emplace([this] { InitDescriptorTables(); });
    tf::Task initCommonResources = tf.emplace([this] { m_CommonResources->Initialize(); });
    tf.emplace([this] { m_Scene->Initialize(); });
    tf.emplace([this] { m_TextureFeedbackManager->Initialize(); });

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

void Graphic::PostSceneLoad()
{
    PROFILE_FUNCTION();
    SCOPED_TIMER_FUNCTION();

    m_Scene->PostSceneLoad();

    for (IRenderer* renderer : IRenderer::ms_AllRenderers)
    {
        PROFILE_SCOPED(renderer->m_Name.c_str());
        LOG_DEBUG("Post Scene Load for Renderer: %s", renderer->m_Name.c_str());
        renderer->PostSceneLoad();
    }
}

void Graphic::Shutdown()
{
    // wait for latest swap chain present to be done
    verify(m_NVRHIDevice->waitForIdle());

    m_Scene->Shutdown();
    m_Scene.reset();

    m_TextureFeedbackManager->Shutdown();
    m_TextureFeedbackManager.reset();

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

        // run as a task due to the usage of "corun" in the InitShaders function
        tf::Taskflow tf;
        tf.emplace([this] { InitShaders(); });
        g_Engine.m_Executor->corun(tf);

        m_bTriggerReloadShaders = false;
    }
    
    if (m_RenderDocAPI)
    {
        const SDL_Keymod keyMod = SDL_GetModState();
        const bool* keyboardStates = SDL_GetKeyboardState(nullptr);
        if ((keyMod & SDL_KMOD_ALT) && 
            (keyboardStates[SDL_SCANCODE_F12]))
        {
            m_RenderDocAPI->TriggerCapture();
        }
    }

    ++m_FrameCounter;

    // execute all cmd lists that may have been potentially added as engine commands
    ExecuteAllCommandLists();

    {
        PROFILE_SCOPED("getTimerQueryTime");
        g_Engine.m_GPUTimeMs = Timer::SecondsToMilliSeconds(m_NVRHIDevice->getTimerQueryTime(m_FrameTimerQuery[m_FrameCounter % 2]));
    }

    {
        nvrhi::CommandListHandle commandList = AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Begin Frame Timer Query");

        g_Graphic.m_NVRHIDevice->resetTimerQuery(m_FrameTimerQuery[m_FrameCounter % 2]);
        commandList->beginTimerQuery(m_FrameTimerQuery[m_FrameCounter % 2]);
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
        nvrhi::CommandListHandle commandList = AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "End Frame Timer Query");
        commandList->endTimerQuery(m_FrameTimerQuery[m_FrameCounter % 2]);
    }

    // execute all cmd lists for this frame
    ExecuteAllCommandLists();

    // finally, present swap chain
    m_GraphicRHI->SwapChainPresent();
}

void Graphic::ExecuteAllCommandLists()
{
    PROFILE_FUNCTION();

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

        if (g_ExecutePerCommandList.Get() || g_ExecuteAndWaitPerCommandList.Get())
        {
            for (nvrhi::CommandListHandle cmdList : m_PendingCommandLists)
            {
                m_NVRHIDevice->executeCommandList(cmdList);

                if (g_ExecuteAndWaitPerCommandList.Get())
                {
                    verify(m_NVRHIDevice->waitForIdle());
                }
            }
        }
        else
        {
            m_NVRHIDevice->executeCommandLists(&m_PendingCommandLists[0], m_PendingCommandLists.size());
        }

        m_PendingCommandLists.clear();
    }
}

void Graphic::AddFullScreenPass(const FullScreenPassParams& fullScreenPassParams)
{
    nvrhi::CommandListHandle commandList = fullScreenPassParams.m_CommandList;
    const nvrhi::FramebufferDesc& frameBufferDesc = fullScreenPassParams.m_FrameBufferDesc;
    const nvrhi::BlendState::RenderTarget* blendStateIn = fullScreenPassParams.m_BlendState;
    const nvrhi::DepthStencilState* depthStencilStateIn = fullScreenPassParams.m_DepthStencilState;
    const nvrhi::Viewport* viewPortIn = fullScreenPassParams.m_ViewPort;
    const void* pushConstantsData = fullScreenPassParams.m_PushConstantsData;
    const size_t pushConstantsBytes = fullScreenPassParams.m_PushConstantsBytes;

    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(commandList, fullScreenPassParams.m_ShaderName.data());

    nvrhi::BlendState blendState;
    blendState.targets[0] = blendStateIn ? *blendStateIn : g_CommonResources.BlendOpaque;

    const nvrhi::DepthStencilState& depthStencilState = depthStencilStateIn ? *depthStencilStateIn : g_CommonResources.DepthNoneStencilNone;

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    g_Graphic.CreateBindingSetAndLayout(fullScreenPassParams.m_BindingSetDesc, bindingSet, bindingLayout);

    // PSO
    nvrhi::MeshletPipelineDesc PSODesc;
    PSODesc.MS = GetShader("fullscreen_MS_FullScreenTriangle");
    PSODesc.PS = GetShader(fullScreenPassParams.m_ShaderName);
    PSODesc.renderState = nvrhi::RenderState{ blendState, depthStencilState, g_CommonResources.CullNone };
    PSODesc.bindingLayouts.push_back(bindingLayout);
    
    for (nvrhi::BindingLayoutHandle extraBindingLayout : fullScreenPassParams.m_ExtraBindingLayouts)
    {
        PSODesc.bindingLayouts.push_back(extraBindingLayout);
    }

    nvrhi::FramebufferHandle frameBuffer = m_NVRHIDevice->createFramebuffer(frameBufferDesc);

    const nvrhi::TextureDesc& renderTargetDesc = frameBufferDesc.colorAttachments.at(0).texture->getDesc();

    const nvrhi::Viewport& viewPort = viewPortIn ? *viewPortIn : nvrhi::Viewport{ (float)renderTargetDesc.width, (float)renderTargetDesc.height };

    nvrhi::MeshletState meshletState;
    meshletState.framebuffer = frameBuffer;
    meshletState.viewport.addViewportAndScissorRect(viewPort);
    meshletState.pipeline = GetOrCreatePSO(PSODesc, frameBuffer);
    meshletState.bindings.push_back(bindingSet);

    for (nvrhi::BindingSetHandle extraBindingSet : fullScreenPassParams.m_ExtraBindingSets)
    {
        meshletState.bindings.push_back(extraBindingSet);
    }

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

    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(computePassParams.m_CommandList, computePassParams.m_ShaderName.data());

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    g_Graphic.CreateBindingSetAndLayout(computePassParams.m_BindingSetDesc, bindingSet, bindingLayout);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.CS = GetShader(computePassParams.m_ShaderName);
    pipelineDesc.bindingLayouts.push_back(bindingLayout);

    for (nvrhi::BindingLayoutHandle extraBindingLayout : computePassParams.m_ExtraBindingLayouts)
    {
        pipelineDesc.bindingLayouts.push_back(extraBindingLayout);
    }

    nvrhi::ComputeState computeState;
    computeState.pipeline = GetOrCreatePSO(pipelineDesc);
    computeState.bindings.push_back(bindingSet);
    
    for (nvrhi::BindingSetHandle extraBindingSet : computePassParams.m_ExtraBindingSets)
    {
        computeState.bindings.push_back(extraBindingSet);
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
