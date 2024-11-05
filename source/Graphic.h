#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "extern/microprofile/microprofile.h"
#include "nvrhi/nvrhi.h"
#include "nvrhi/utils.h"

#if NVRHI_WITH_AFTERMATH
#include "AftermathCrashDump.h"
#endif

#include "Allocators.h"
#include "CriticalSection.h"
#include "MathUtilities.h"
#include "Visual.h"

class CommonResources;
class DescriptorTableManager;
class RenderGraph;
class Scene;
struct MaterialData;

class Graphic
{
public:
    SingletonFunctionsSimple(Graphic);

    static constexpr uint32_t kMaxTextureMipsToGenerate = 12;
    static constexpr uint32_t kNbCSMCascades = 4;

    static constexpr uint32_t kStencilBit_Opaque = 0x0;
    static constexpr uint32_t kStencilBit_Sky = 0x1;

    static constexpr bool kFrontCCW = true;
    static constexpr bool kInversedDepthBuffer = true;
    static constexpr bool kInfiniteDepthBuffer = true;
    static constexpr bool kInversedShadowMapDepthBuffer = true;

    static constexpr float kNearDepth = kInversedDepthBuffer ? 1.0f : 0.0f;
    static constexpr float kFarDepth = 1.0f - kNearDepth;
    static constexpr float kNearShadowMapDepth = kInversedShadowMapDepthBuffer ? 1.0f : 0.0f;
    static constexpr float kFarShadowMapDepth = 1.0f - kNearShadowMapDepth;
    static constexpr float kDefaultCameraNearPlane = 0.1f;

    static constexpr nvrhi::Format kDepthStencilFormat = nvrhi::Format::D24S8;
    static constexpr nvrhi::Format kDepthBufferCopyFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kHZBFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kShadowMapFormat = nvrhi::Format::D16;
    static constexpr nvrhi::Format kIndexBufferFormat = nvrhi::Format::R32_UINT;
    static constexpr nvrhi::Format kGBufferAFormat = nvrhi::Format::RGBA8_UNORM; // albedo
    static constexpr nvrhi::Format kGBufferBFormat = nvrhi::Format::RG16_UNORM; // normals
    static constexpr nvrhi::Format kGBufferCFormat = nvrhi::Format::RGBA8_UNORM; // PBR
    static constexpr nvrhi::Format kLightingOutputFormat = nvrhi::Format::R11G11B10_FLOAT;
    static constexpr nvrhi::Format kSSAOOutputFormat = nvrhi::Format::R8_UINT;

    using IndexBufferFormat_t = uint32_t;

    void Initialize();
    void Shutdown();
    void Update();
    void InitDevice();
    void InitSwapChain();
    void InitShaders();
    void InitDescriptorTable();
    void InitVirtualBuffers();
    void Present();

    // same functionality as the one from Engine.h to prevent unnecessary #include
    static uint32_t GetThreadID();

    static void UpdateResourceDebugName(nvrhi::IResource* resource, std::string_view debugName);
    
    [[nodiscard]] nvrhi::TextureHandle GetCurrentBackBuffer() { return m_SwapChainTextureHandles[m_SwapChain->GetCurrentBackBufferIndex()]; }
    [[nodiscard]] nvrhi::ShaderHandle GetShader(std::string_view shaderBinName);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindingLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::BindingLayoutHandle GetOrCreateBindingLayout(const nvrhi::BindlessLayoutDesc& layoutDesc);
    [[nodiscard]] nvrhi::GraphicsPipelineHandle GetOrCreatePSO(const nvrhi::GraphicsPipelineDesc& psoDesc, nvrhi::FramebufferHandle frameBuffer);
    [[nodiscard]] nvrhi::ComputePipelineHandle GetOrCreatePSO(const nvrhi::ComputePipelineDesc& psoDesc);
    uint32_t AppendOrRetrieveMaterialDataIndex(const MaterialData& materialData);

    Mesh* CreateMesh();

    void CreateBindingSetAndLayout(const nvrhi::BindingSetDesc& bindingSetDesc, nvrhi::BindingSetHandle& outBindingSetHandle, nvrhi::BindingLayoutHandle& outLayoutHandle);

    template <typename T>
    [[nodiscard]] nvrhi::BufferHandle CreateConstantBuffer(nvrhi::CommandListHandle commandList, const T& srcData)
    {
        nvrhi::BufferHandle buffer = m_NVRHIDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(T), std::type_index{ typeid(T) }.name(), 1));
        commandList->writeBuffer(buffer, &srcData, sizeof(T));
        return buffer;
    }

    [[nodiscard]]  nvrhi::CommandListHandle AllocateCommandList(nvrhi::CommandQueue queueType = nvrhi::CommandQueue::Graphics);
    void FreeCommandList(nvrhi::CommandListHandle cmdList);
    void BeginCommandList(nvrhi::CommandListHandle cmdList, std::string_view name);
    void EndCommandList(nvrhi::CommandListHandle cmdList, bool bQueueCmdlist);
    void ExecuteAllCommandLists();
    void QueueCommandList(nvrhi::CommandListHandle commandList) { AUTO_LOCK(m_PendingCommandListsLock); m_PendingCommandLists.push_back(commandList); }

    void AddFullScreenPass(
        nvrhi::CommandListHandle commandList,
        const nvrhi::FramebufferDesc& frameBufferDesc,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        std::string_view pixelShaderName,
        const nvrhi::BlendState::RenderTarget* blendState = nullptr,
        const nvrhi::DepthStencilState* depthStencilState = nullptr,
        const nvrhi::Viewport* viewPort = nullptr,
        const void* pushConstantsData = nullptr,
        size_t pushConstantsBytes = 0);

    void AddComputePass(
        nvrhi::CommandListHandle commandList,
        std::string_view shaderName,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        const Vector3U& dispatchGroupSize,
        nvrhi::BufferHandle indirectArgsBuffer,
        uint32_t indirectArgsBufferOffsetBytes,
        nvrhi::BufferHandle indirectCountBuffer,
        uint32_t indirectCountBufferOffsetBytes,
        const void* pushConstantsData,
        size_t pushConstantsBytes);

    void AddComputePass(
        nvrhi::CommandListHandle commandList,
        std::string_view shaderName,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        const Vector3U& dispatchGroupSize,
        const void* pushConstantsData = nullptr,
        size_t pushConstantsBytes = 0)
    {
        AddComputePass(
            commandList,
            shaderName,
            bindingSetDesc,
            dispatchGroupSize,
            nvrhi::BufferHandle{},
            0,
            nvrhi::BufferHandle{},
            0,
            pushConstantsData,
            pushConstantsBytes);
    }

    void AddComputePass(
        nvrhi::CommandListHandle commandList,
        std::string_view shaderName,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        nvrhi::BufferHandle indirectArgsBuffer,
        uint32_t indirectArgsBufferOffsetBytes = 0,
        nvrhi::BufferHandle indirectCountBuffer = {},
        uint32_t indirectCountBufferOffsetBytes = 0,
        const void* pushConstantsData = nullptr,
        size_t pushConstantsBytes = 0)
    {
        AddComputePass(
            commandList,
            shaderName,
            bindingSetDesc,
            Vector3U{ 0, 0, 0 },
            indirectArgsBuffer,
            indirectArgsBufferOffsetBytes,
            indirectCountBuffer,
            indirectCountBufferOffsetBytes,
            pushConstantsData,
            pushConstantsBytes);
    }

    nvrhi::DeviceHandle m_NVRHIDevice;

    std::shared_ptr<Scene> m_Scene;
    std::shared_ptr<CommonResources> m_CommonResources;

    nvrhi::TextureHandle m_SwapChainTextureHandles[2];

    // simple bindless layout that can store up to 1024 SRVs in (t0,space1)
    static const uint32_t kBindlessLayoutCapacity = 1024;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    std::shared_ptr<DescriptorTableManager> m_DescriptorTableManager;

    std::mutex m_MeshesArrayLock;
    std::vector<Mesh> m_Meshes;

    GrowableGPUVirtualBuffer m_VirtualVertexBuffer;
    GrowableGPUVirtualBuffer m_VirtualIndexBuffer;
    GrowableGPUVirtualBuffer m_VirtualMeshDataBuffer;
    GrowableGPUVirtualBuffer m_VirtualMaterialDataBuffer;

    Vector2U m_RenderResolution;
    Vector2U m_DisplayResolution;

    uint32_t m_FrameCounter = 0;
    bool m_bTriggerReloadShaders = false;

    std::vector<MicroProfileThreadLogGpu*> m_GPUThreadLogs;

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
    std::unordered_map<size_t, nvrhi::ComputePipelineHandle> m_CachedComputePSOs;
    std::unordered_map<size_t, nvrhi::BindingLayoutHandle> m_CachedBindingLayouts;
    std::unordered_map<size_t, uint32_t> m_CachedMaterialDataIndices;
    
    std::mutex m_PendingCommandListsLock;
    std::vector<nvrhi::CommandListHandle> m_PendingCommandLists;

    nvrhi::TimerQueryHandle m_FrameTimerQuery;

#if NVRHI_WITH_AFTERMATH
    AftermathCrashDump m_AftermathCrashDumper;
    std::pair<const void*, size_t> FindShaderFromHashForAftermath(uint64_t hash, std::function<uint64_t(std::pair<const void*, size_t>, nvrhi::GraphicsAPI)> hashGenerator);
#endif // NVRHI_WITH_AFTERMATH
};
#define g_Graphic Graphic::GetInstance()

class FencedReadbackBuffer
{
public:
    static const uint32_t kNbBuffers = 3;

    static uint32_t GetWriteIndex();
    static uint32_t GetReadIndex();

    void Initialize(nvrhi::DeviceHandle device, uint32_t bufferSize);
    void CopyTo(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList, nvrhi::BufferHandle bufferSource, nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics);
    void Read(nvrhi::DeviceHandle device, void* outPtr);

    uint32_t m_BufferSize = 0;
    nvrhi::BufferHandle m_Buffers[kNbBuffers];
    nvrhi::EventQueryHandle m_EventQueries[kNbBuffers];
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

struct ScopedCommandListMarker
{
    ScopedCommandListMarker(nvrhi::CommandListHandle cmdList, std::string_view name)
        : m_CommandList(cmdList)
    {
        m_CommandList->beginMarker(name.data());
    }

    ~ScopedCommandListMarker()
    {
        m_CommandList->endMarker();
    }

    nvrhi::CommandListHandle m_CommandList;
};

// Helpers for dispatch compute shader threads
namespace ComputeShaderUtils
{
    constexpr Vector3U GetGroupCount(uint32_t ThreadCount, uint32_t GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount, GroupSize), 1, 1 }; }
    constexpr Vector3U GetGroupCount(Vector2U ThreadCount, Vector2U GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize.x), DivideAndRoundUp(ThreadCount.y, GroupSize.y), 1 }; }
    constexpr Vector3U GetGroupCount(Vector2U ThreadCount, uint32_t GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize), DivideAndRoundUp(ThreadCount.y, GroupSize), 1 }; }
    constexpr Vector3U GetGroupCount(Vector3U ThreadCount, Vector3U GroupSize) { return Vector3U{ DivideAndRoundUp(ThreadCount.x, GroupSize.x), DivideAndRoundUp(ThreadCount.y, GroupSize.y), DivideAndRoundUp(ThreadCount.z, GroupSize.z) }; }
}

uint32_t ComputeNbMips(uint32_t width, uint32_t height);
uint32_t BytesPerPixel(nvrhi::Format Format);
std::size_t HashResourceDesc(const nvrhi::TextureDesc& desc);
std::size_t HashResourceDesc(const nvrhi::BufferDesc& desc);

#define PROFILE_GPU_SCOPED(cmdList, NAME) \
    const ScopedCommandListMarker GENERATE_UNIQUE_VARIABLE(ScopedCommandListMarker) { cmdList, NAME }; \
    MicroProfileToken MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__) = MicroProfileGetToken("GPU", NAME, (uint32_t)std::hash<std::string_view>{}(NAME), MicroProfileTokenTypeGpu, 0); \
    MicroProfileScopeGpuHandler GENERATE_UNIQUE_VARIABLE(MicroProfileScopeGpuHandler){ MICROPROFILE_TOKEN_PASTE(__Microprofile_GPU_Token__, __LINE__), g_Graphic.m_GPUThreadLogs.at(Graphic::GetThreadID()) };

#define SCOPED_COMMAND_LIST(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList){ commandList, NAME, false /*bAutoQueue*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)

#define SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, NAME) \
    ScopedCommandList GENERATE_UNIQUE_VARIABLE(scopedCommandList) { commandList, NAME, true /*bAutoQueue*/ }; \
    PROFILE_GPU_SCOPED(commandList, NAME)
