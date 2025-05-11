#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "extern/microprofile/microprofile.h"
#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvrhi/include/nvrhi/utils.h"
#include "extern/renderdoc/renderdoc_app.h"

#if NVRHI_WITH_AFTERMATH
#include "AftermathCrashDump.h"
#endif

#include "CriticalSection.h"
#include "MathUtilities.h"
#include "Visual.h"

class CommonResources;
class DescriptorTableManager;
class RenderGraph;
class Scene;
struct MaterialData;

class GPUTimerQuery
{
public:
    void Initialize();
    void Begin(nvrhi::CommandListHandle commandList);
    void End(nvrhi::CommandListHandle commandList);
    float GetLastValid() const; // in MilliSeconds

private:
    static const uint32_t kQueuedFramesCount = 5;
    nvrhi::TimerQueryHandle m_TimerQueryHandles[kQueuedFramesCount];
    uint32_t m_Counter = 0;
};

class Graphic
{
public:
    SingletonFunctionsSimple(Graphic);

    static constexpr uint32_t kMaxTextureMipsToGenerate = 12;
    static constexpr uint32_t kMaxThreadGroupsPerDimension = 65535; // both d3d12 & vulkan have a limit of 65535 thread groups per dimension
    static constexpr uint32_t kMaxNumMeshLODs = 8;

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
    void InitSwapChain();
    void InitShaders();
    void InitDescriptorTable();
    void Present();
    void SetGPUStablePowerState(bool bEnable);

    static void UpdateResourceDebugName(nvrhi::IResource* resource, std::string_view debugName);
    
    [[nodiscard]] nvrhi::TextureHandle GetCurrentBackBuffer() { return m_SwapChainTextureHandles[m_SwapChain->GetCurrentBackBufferIndex()]; }
    [[nodiscard]] nvrhi::ShaderHandle GetShader(std::string_view shaderBinName);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindingLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindlessLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::GraphicsPipelineHandle GetOrCreatePSO(const nvrhi::GraphicsPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer);
    [[nodiscard]] nvrhi::MeshletPipelineHandle GetOrCreatePSO(const nvrhi::MeshletPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer);
    [[nodiscard]] nvrhi::ComputePipelineHandle GetOrCreatePSO(const nvrhi::ComputePipelineDesc& psoDesc);
    [[nodiscard]] nvrhi::rt::PipelineHandle GetOrCreatePSO(const nvrhi::rt::PipelineDesc& psoDesc);

    void CreateBindingSetAndLayout(const nvrhi::BindingSetDesc& bindingSetDesc, nvrhi::BindingSetHandle& outBindingSetHandle, nvrhi::BindingLayoutHandle& outLayoutHandle);

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

    struct FullScreenPassParams
    {
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::FramebufferDesc m_FrameBufferDesc;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        std::string_view m_PixelShaderName;
        const nvrhi::BlendState::RenderTarget* m_BlendState = nullptr;
        const nvrhi::DepthStencilState* m_DepthStencilState = nullptr;
        const nvrhi::Viewport* m_ViewPort = nullptr;
        const void* m_PushConstantsData = nullptr;
        size_t m_PushConstantsBytes = 0;
    };

    void AddFullScreenPass(const FullScreenPassParams& fullScreenPassParams);

    struct ComputePassParams
    {
        nvrhi::CommandListHandle m_CommandList;
        std::string_view m_ShaderName;
        nvrhi::BindingSetDesc m_BindingSetDesc; // TODO: remove
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        Vector3U m_DispatchGroupSize = Vector3U{ 0, 0, 0 };
        nvrhi::BufferHandle m_IndirectArgsBuffer;
        uint32_t m_IndirectArgsBufferOffsetBytes = 0;
        const void* m_PushConstantsData = nullptr;
        size_t m_PushConstantsBytes = 0;
        bool m_ShouldAddBindlessResources = false;
    };

    void AddComputePass(const ComputePassParams& computePassParams);

    Vector2 GetCurrentJitterOffset();

    nvrhi::DeviceHandle m_NVRHIDevice;
    RENDERDOC_API_1_6_0* m_RenderDocAPI = nullptr;

    std::shared_ptr<Scene> m_Scene;
    std::shared_ptr<CommonResources> m_CommonResources;

    nvrhi::TextureHandle m_SwapChainTextureHandles[2];

    // simple bindless layout that can store up to 1024 SRVs in (t0,space1)
    static const uint32_t kBindlessLayoutCapacity = 1024;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    std::shared_ptr<DescriptorTableManager> m_DescriptorTableManager;

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

private:
    bool m_bTearingSupported = false;

    ComPtr<ID3D12CommandQueue> m_ComputeQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12Device> m_D3DDevice;
    ComPtr<ID3D12Resource> m_SwapChainD3D12Resources[2];
    ComPtr<IDXGIAdapter1> m_DXGIAdapter;
    ComPtr<IDXGIFactory6> m_DXGIFactory;
    ComPtr<IDXGISwapChain3> m_SwapChain;

    int m_GPUQueueLogs[(uint32_t)nvrhi::CommandQueue::Count];
    
    std::unordered_map<size_t, nvrhi::ShaderHandle> m_AllShaders;
    std::unordered_map<size_t, nvrhi::GraphicsPipelineHandle> m_CachedGraphicPSOs;
    std::unordered_map<size_t, nvrhi::MeshletPipelineHandle> m_CachedMeshletPSOs;
    std::unordered_map<size_t, nvrhi::ComputePipelineHandle> m_CachedComputePSOs;
    std::unordered_map<size_t, nvrhi::rt::PipelineHandle> m_CachedRTPSOs;
    std::unordered_map<size_t, nvrhi::BindingLayoutHandle> m_CachedBindingLayouts;
    
    std::mutex m_PendingCommandListsLock;
    std::vector<nvrhi::CommandListHandle> m_PendingCommandLists;

    GPUTimerQuery m_FrameTimerQuery;

#if NVRHI_WITH_AFTERMATH
    AftermathCrashDump m_AftermathCrashDumper;
    std::pair<const void*, size_t> FindShaderFromHashForAftermath(uint64_t hash, std::function<uint64_t(std::pair<const void*, size_t>, nvrhi::GraphicsAPI)> hashGenerator);
#endif // NVRHI_WITH_AFTERMATH
};
#define g_Graphic Graphic::GetInstance()

class FencedReadbackResource
{
public:
    static const uint32_t kNbResources = 3;

    static uint32_t GetWriteIndex() { return g_Graphic.m_FrameCounter % kNbResources; }
    static uint32_t GetReadIndex() { return (g_Graphic.m_FrameCounter + 1) % kNbResources; }

    virtual void Read(void* ptr) = 0;

    nvrhi::EventQueryHandle m_EventQueries[kNbResources];
};

class FencedReadbackBuffer : public FencedReadbackResource
{
public:
    void Initialize(uint32_t bufferSize);
    void CopyTo(nvrhi::CommandListHandle commandList, nvrhi::BufferHandle bufferSource);
    void Read(void* outPtr) override;

    uint32_t m_BufferSize = 0;
    nvrhi::BufferHandle m_Buffers[kNbResources];
};

class FencedReadbackTexture : public FencedReadbackResource
{
public:
    void Initialize(nvrhi::Format format);
    void CopyTo(nvrhi::CommandListHandle commandList, nvrhi::TextureHandle textureSource);
    void Read(void* outPtr) override;

    nvrhi::StagingTextureHandle m_StagingTexture[kNbResources];
};

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
