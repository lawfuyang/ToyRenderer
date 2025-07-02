#pragma once

#include "extern/microprofile/microprofile.h"
#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvrhi/include/nvrhi/utils.h"
#include "extern/renderdoc/renderdoc_app.h"

#include "CriticalSection.h"
#include "DescriptorTableManager.h"
#include "MathUtilities.h"
#include "Visual.h"
#include "SmallVector.h"

class CommonResources;
class RenderGraph;
class Scene;
struct MaterialData;

class GraphicRHI
{
public:
    virtual ~GraphicRHI() = default;

    static GraphicRHI* Create();
    
    virtual nvrhi::DeviceHandle CreateDevice() = 0;
    virtual void InitSwapChainTextureHandles() = 0;
    virtual uint32_t GetCurrentBackBufferIndex() = 0;
    virtual void SwapChainPresent() = 0;
    virtual void* GetNativeCommandList(nvrhi::CommandListHandle commandList) = 0;

    virtual void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) = 0;
    virtual void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) = 0;
};

class Graphic
{
public:
    SingletonFunctionsSimple(Graphic);

    static constexpr uint32_t kMaxTextureMips = 16;
    static constexpr uint32_t kMaxThreadGroupsPerDimension = 65535; // both d3d12 & vulkan have a limit of 65535 thread groups per dimension
    static constexpr uint32_t kMaxNumMeshLODs = 8;
    static constexpr uint32_t kPackedMipResolution = 256;

    static constexpr uint32_t kStencilBit_Opaque = 0x0;
    static constexpr uint32_t kStencilBit_Sky = 0x1;

    static constexpr bool kFrontCCW = true;
    static constexpr bool kInversedDepthBuffer = true;
    static constexpr bool kInfiniteDepthBuffer = true;

    static constexpr float kNearDepth = kInversedDepthBuffer ? 1.0f : 0.0f;
    static constexpr float kFarDepth = 1.0f - kNearDepth;
    static constexpr float kDefaultCameraNearPlane = 0.1f;

    static constexpr nvrhi::Format kGBufferAFormat = nvrhi::Format::RGBA32_UINT;
    static constexpr nvrhi::Format kGBufferMotionFormat = nvrhi::Format::RG16_FLOAT;
    static constexpr nvrhi::Format kDepthStencilFormat = nvrhi::Format::D24S8;
    static constexpr nvrhi::Format kDepthBufferCopyFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kHZBFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kIndexBufferFormat = nvrhi::Format::R32_UINT;
    static constexpr nvrhi::Format kLightingOutputFormat = nvrhi::Format::R11G11B10_FLOAT;
    static constexpr nvrhi::Format kSSAOOutputFormat = nvrhi::Format::R8_UINT;

    using IndexBufferFormat_t = uint32_t;

    void Initialize();
    void PostSceneLoad();
    void Shutdown();
    void Update();
    void InitRenderDocAPI();
    void InitDevice();
    void InitShaders();
    void InitDescriptorTables();
    
    [[nodiscard]] nvrhi::TextureHandle GetCurrentBackBuffer();
    [[nodiscard]] nvrhi::ShaderHandle GetShader(std::string_view shaderBinName);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindingLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindlessLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::GraphicsPipelineHandle GetOrCreatePSO(const nvrhi::GraphicsPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer);
    [[nodiscard]] nvrhi::MeshletPipelineHandle GetOrCreatePSO(const nvrhi::MeshletPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer);
    [[nodiscard]] nvrhi::ComputePipelineHandle GetOrCreatePSO(const nvrhi::ComputePipelineDesc& psoDesc);

    nvrhi::IDescriptorTable* GetSrvUavCbvDescriptorTable();
    void RegisterInSrvUavCbvDescriptorTable(nvrhi::TextureHandle texture);
    uint32_t GetIndexInHeap(uint32_t indexInTable) const;

    void CreateBindingSetAndLayout(const nvrhi::BindingSetDesc& bindingSetDesc, nvrhi::BindingSetHandle& outBindingSetHandle, nvrhi::BindingLayoutHandle& outLayoutHandle, uint32_t registerSpace = 0);

    template <typename T>
    [[nodiscard]] nvrhi::BufferHandle CreateConstantBuffer(nvrhi::CommandListHandle commandList, const T& srcData)
    {
        nvrhi::BufferHandle buffer = m_NVRHIDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(T), std::type_index{ typeid(T) }.name(), 1));
        commandList->writeBuffer(buffer, &srcData, sizeof(T));
        return buffer;
    }

    [[nodiscard]] nvrhi::CommandListHandle AllocateCommandList(nvrhi::CommandQueue queueType = nvrhi::CommandQueue::Graphics);
    void FreeCommandList(nvrhi::CommandListHandle cmdList);
    void BeginCommandList(nvrhi::CommandListHandle cmdList, std::string_view name);
    void EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist);
    void ExecuteAllCommandLists();
    void QueueCommandList(nvrhi::CommandListHandle commandList) { AUTO_LOCK(m_PendingCommandListsLock); m_PendingCommandLists.push_back(commandList); }

    struct AddPassParamsCommon
    {
        nvrhi::CommandListHandle m_CommandList;
        std::string m_ShaderName;
        nvrhi::BindingSetDesc m_BindingSetDesc;
        SmallVector<nvrhi::BindingSetHandle, 1> m_ExtraBindingSets;
        SmallVector<nvrhi::BindingLayoutHandle, 1> m_ExtraBindingLayouts;
        const void* m_PushConstantsData = nullptr;
        size_t m_PushConstantsBytes = 0;
    };

    struct FullScreenPassParams : public AddPassParamsCommon
    {
        nvrhi::FramebufferDesc m_FrameBufferDesc;
        const nvrhi::BlendState::RenderTarget* m_BlendState = nullptr;
        const nvrhi::DepthStencilState* m_DepthStencilState = nullptr;
        const nvrhi::Viewport* m_ViewPort = nullptr;
    };

    void AddFullScreenPass(const FullScreenPassParams& fullScreenPassParams);

    struct ComputePassParams : public AddPassParamsCommon
    {
        Vector3U m_DispatchGroupSize = Vector3U{ 0, 0, 0 };
        nvrhi::BufferHandle m_IndirectArgsBuffer;
        uint32_t m_IndirectArgsBufferOffsetBytes = 0;
    };

    void AddComputePass(const ComputePassParams& computePassParams);

    Vector2 GetCurrentJitterOffset();

    nvrhi::DeviceHandle m_NVRHIDevice;
    RENDERDOC_API_1_6_0* m_RenderDocAPI = nullptr;

    std::shared_ptr<Scene> m_Scene;
    std::shared_ptr<CommonResources> m_CommonResources;

    nvrhi::TextureHandle m_SwapChainTextureHandles[2];

    static const uint32_t kSrvUavCbvBindlessLayoutCapacity = 1024; // NOTE: increase if needed
    nvrhi::BindingLayoutHandle m_SrvUavCbvBindlessLayout;
    std::shared_ptr<DescriptorTableManager> m_SrvUavCbvDescriptorTableManager;

    std::vector<Mesh> m_Meshes;

    nvrhi::BufferHandle m_GlobalVertexBuffer;
    nvrhi::BufferHandle m_GlobalIndexBuffer;
    nvrhi::BufferHandle m_GlobalMeshDataBuffer;
    nvrhi::BufferHandle m_GlobalMaterialDataBuffer;
    nvrhi::BufferHandle m_GlobalMeshletVertexOffsetsBuffer;
    nvrhi::BufferHandle m_GlobalMeshletIndicesBuffer;
    nvrhi::BufferHandle m_GlobalMeshletDataBuffer;

    Vector2U m_RenderResolution;
    Vector2U m_DisplayResolution;

    uint32_t m_FrameCounter = 0;
    bool m_bTriggerReloadShaders = false;

    std::unordered_map<std::thread::id, MicroProfileThreadLogGpu*> m_GPUThreadLogs;

    std::vector<nvrhi::CommandListHandle> m_AllCommandLists[(uint32_t)nvrhi::CommandQueue::Count];
    std::deque<nvrhi::CommandListHandle> m_FreeCommandLists[(uint32_t)nvrhi::CommandQueue::Count];
    std::mutex m_FreeCommandListsLock;

    int m_GPUQueueLogs[(uint32_t)nvrhi::CommandQueue::Count];

private:
    std::unique_ptr<GraphicRHI> m_GraphicRHI;

    bool m_bTearingSupported = false;
    
    std::unordered_map<size_t, nvrhi::ShaderHandle> m_AllShaders;
    std::unordered_map<size_t, nvrhi::GraphicsPipelineHandle> m_CachedGraphicPSOs;
    std::unordered_map<size_t, nvrhi::MeshletPipelineHandle> m_CachedMeshletPSOs;
    std::unordered_map<size_t, nvrhi::ComputePipelineHandle> m_CachedComputePSOs;
    std::unordered_map<size_t, nvrhi::rt::PipelineHandle> m_CachedRTPSOs;
    std::unordered_map<size_t, nvrhi::BindingLayoutHandle> m_CachedBindingLayouts;
    
    std::mutex m_PendingCommandListsLock;
    std::vector<nvrhi::CommandListHandle> m_PendingCommandLists;

    nvrhi::TimerQueryHandle m_FrameTimerQuery[2];
};
#define g_Graphic Graphic::GetInstance()

class IRenderer
{
public:
    IRenderer(const char* rendererName)
        : m_Name(rendererName)
    {
        ms_AllRenderers.push_back(this);
    }

    virtual ~IRenderer() = default;
    virtual void Initialize() {};
    virtual void PostSceneLoad() {};
    virtual void UpdateImgui() {};

    // return false if the renderer is not going to be used
    virtual bool Setup(RenderGraph& renderGraph) { return true; }

    virtual void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) = 0;

    const std::string m_Name;

    float m_CPUFrameTime = 0.0f;
    float m_GPUFrameTime = 0.0f;
    nvrhi::TimerQueryHandle m_FrameTimerQuery[2];

    inline static std::vector<IRenderer*> ms_AllRenderers;
};

struct ScopedCommandList
{
    ScopedCommandList(nvrhi::CommandListHandle cmdList, std::string_view name, bool bAutoQueue)
        : m_CommandList(cmdList)
        , m_AutoQueue(bAutoQueue)
    {
        g_Graphic.BeginCommandList(cmdList, name);
    }

    ~ScopedCommandList()
    {
        g_Graphic.EndCommandList(m_CommandList, m_AutoQueue);
    }

    nvrhi::CommandListHandle m_CommandList;
    const bool m_AutoQueue;
};

// Helpers for dispatch compute shader threads
namespace ComputeShaderUtils
{
    constexpr Vector3U GetGroupCount(uint32_t ThreadCount, uint32_t GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount, GroupSize), 1, 1 }; }
    constexpr Vector3U GetGroupCount(Vector2U ThreadCount, Vector2U GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize.x), DivideAndRoundUp(ThreadCount.y, GroupSize.y), 1 }; }
    constexpr Vector3U GetGroupCount(Vector2U ThreadCount, uint32_t GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize), DivideAndRoundUp(ThreadCount.y, GroupSize), 1 }; }
    constexpr Vector3U GetGroupCount(Vector3U ThreadCount, Vector3U GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize.x), DivideAndRoundUp(ThreadCount.y, GroupSize.y), DivideAndRoundUp(ThreadCount.z, GroupSize.z) }; }
}

constexpr uint32_t ComputeNbMips(uint32_t width, uint32_t height)
{
    const uint32_t resolution = std::max(width, height);
    return resolution == 1 ? 1 : (uint32_t)std::floor(std::log2(resolution));
}

#define PROFILE_GPU_SCOPED(cmdList, NAME) \
    nvrhi::utils::ScopedMarker GENERATE_UNIQUE_VARIABLE(nvrhi_utils_ScopedMarker){ cmdList, NAME }; \
    MicroProfileToken MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__) = MicroProfileGetToken("GPU", NAME, (uint32_t)std::hash<std::string_view>{}(NAME), MicroProfileTokenTypeGpu, 0); \
    MicroProfileScopeGpuHandler GENERATE_UNIQUE_VARIABLE(MicroProfileScopeGpuHandler){ MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__), g_Graphic.m_GPUThreadLogs.at(std::this_thread::get_id()) };

#define SCOPED_COMMAND_LIST(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList){ commandList, NAME, false /*bAutoQueue*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)

#define SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList) { commandList, NAME, true /*bAutoQueue*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)

#define SCOPED_RENDERDOC_CAPTURE(condition) const ScopedRenderDocCapture GENERATE_UNIQUE_VARIABLE(scopedRenderDocCapture){ condition };
