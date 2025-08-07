#pragma once

#include "extern/microprofile/microprofile.h"
#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvrhi/include/nvrhi/utils.h"
#include "extern/renderdoc/renderdoc_app.h"

#include "CriticalSection.h"
#include "DescriptorTableManager.h"
#include "GraphicConstants.h"
#include "MathUtilities.h"
#include "Utilities.h"
#include "Visual.h"

class CommonResources;
class RenderGraph;
class Scene;
class TextureFeedbackManager;
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
    virtual uint32_t GetTiledResourceSizeInBytes() = 0;
    virtual uint32_t GetMaxTextureDimension() = 0;
    virtual uint32_t GetMaxNumTextureMips() = 0;
    virtual uint32_t GetMaxThreadGroupsPerDimension() = 0;
    virtual uint64_t GetUsedVideoMemory() = 0;

    virtual void SetRHIObjectDebugName(nvrhi::CommandListHandle commandList, std::string_view debugName) = 0;
    virtual void SetRHIObjectDebugName(nvrhi::ResourceHandle resource, std::string_view debugName) = 0;
};

class Graphic
{
public:
    SingletonFunctionsSimple(Graphic);

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
    void EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist, bool bImmediateExecute);
    void ExecuteAllCommandLists();
    void QueueCommandList(nvrhi::CommandListHandle commandList) { AUTO_LOCK(m_PendingCommandListsLock); m_PendingCommandLists.push_back(commandList); }

    static MicroProfileThreadLogGpu*& GetGPULogForCurrentThread();

    struct AddPassParamsCommon
    {
        nvrhi::CommandListHandle m_CommandList;
        std::string m_ShaderName;
        nvrhi::BindingSetDesc m_BindingSetDesc;
        std::vector<nvrhi::BindingSetHandle> m_ExtraBindingSets;
        std::vector<nvrhi::BindingLayoutHandle> m_ExtraBindingLayouts;
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

    std::unique_ptr<GraphicRHI> m_GraphicRHI;

    nvrhi::DeviceHandle m_NVRHIDevice;
    RENDERDOC_API_1_6_0* m_RenderDocAPI = nullptr;

    std::shared_ptr<Scene> m_Scene;
    std::shared_ptr<CommonResources> m_CommonResources;
    std::shared_ptr<TextureFeedbackManager> m_TextureFeedbackManager;

    nvrhi::TextureHandle m_SwapChainTextureHandles[2];

    nvrhi::BindingLayoutHandle m_SrvUavCbvBindlessLayout;
    std::shared_ptr<DescriptorTableManager> m_SrvUavCbvDescriptorTableManager;

    std::vector<Mesh> m_Meshes;
    std::vector<Texture> m_Textures;

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
    float m_GraphicUpdateTimerMs = 0.0f;
    bool m_bTriggerReloadShaders = false;

    std::vector<nvrhi::CommandListHandle> m_AllCommandLists[(uint32_t)nvrhi::CommandQueue::Count];
    std::deque<nvrhi::CommandListHandle> m_FreeCommandLists[(uint32_t)nvrhi::CommandQueue::Count];
    std::mutex m_FreeCommandListsLock;

private:
    std::unordered_map<size_t, nvrhi::ShaderHandle> m_AllShaders;
    std::unordered_map<size_t, nvrhi::GraphicsPipelineHandle> m_CachedGraphicPSOs;
    std::unordered_map<size_t, nvrhi::MeshletPipelineHandle> m_CachedMeshletPSOs;
    std::unordered_map<size_t, nvrhi::ComputePipelineHandle> m_CachedComputePSOs;
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
    ScopedCommandList(nvrhi::CommandListHandle cmdList, std::string_view name, bool bAutoQueue, bool bImmediateExecute)
        : m_CommandList(cmdList)
        , m_bAutoQueue(bAutoQueue)
        , m_bImmediateExecute(bImmediateExecute)
    {
        assert(!(m_bAutoQueue && m_bImmediateExecute)); // cannot queue & execute immediately at the same time
        g_Graphic.BeginCommandList(cmdList, name);
    }

    ~ScopedCommandList()
    {
        g_Graphic.EndCommandList(m_CommandList, m_bAutoQueue, m_bImmediateExecute);
    }

    nvrhi::CommandListHandle m_CommandList;
    const bool m_bAutoQueue;
    const bool m_bImmediateExecute;
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
    const uint32_t resolution = width > height ? width : height;
    return std::bit_width(resolution);
}

#define PROFILE_GPU_SCOPED(cmdList, NAME) \
    nvrhi::utils::ScopedMarker GENERATE_UNIQUE_VARIABLE(nvrhi_utils_ScopedMarker){ cmdList, NAME }; \
    MicroProfileToken MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__) = MicroProfileGetToken("GPU", NAME, (uint32_t)std::hash<std::string_view>{}(NAME), MicroProfileTokenTypeGpu, 0); \
    MicroProfileScopeGpuHandler GENERATE_UNIQUE_VARIABLE(MicroProfileScopeGpuHandler){ MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__), Graphic::GetGPULogForCurrentThread() };

#define SCOPED_COMMAND_LIST(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList){ commandList, NAME, false /*bAutoQueue*/, false /*bImmediateExecute*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)

#define SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList) { commandList, NAME, true /*bAutoQueue*/, false /*bImmediateExecute*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)

#define SCOPED_COMMANDLIST_IMMEDIATE_EXECUTE(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList){ commandList, NAME, false /*bAutoQueue*/, true /*bImmediateExecute*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME); \

#define SCOPED_RENDERDOC_CAPTURE(condition) const ScopedRenderDocCapture GENERATE_UNIQUE_VARIABLE(scopedRenderDocCapture){ condition };
