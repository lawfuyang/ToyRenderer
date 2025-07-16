#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvidia/RTXTS-TTM/include/rtxts-ttm/TiledTextureManager.h"

class TextureFeedbackManager
{
public:
    SingletonFunctionsSimple(TextureFeedbackManager);

    void Initialize();
    void Shutdown();
    void UpdateIMGUI();
    void BeginFrame();
    void ResolveFeedback();

    std::unique_ptr<rtxts::TiledTextureManager> m_TiledTextureManager;
    std::mutex m_TiledTextureManagerLock;

    std::vector<uint32_t> m_TexturesToReadback;
    std::vector<nvrhi::HeapHandle> m_Heaps;
    std::vector<nvrhi::BufferHandle> m_Buffers;
    std::vector<uint32_t> m_FreeHeapIDs;

    rtxts::Statistics m_Statistics;
    uint64_t m_HeapAllocationInBytes = 0;
    float m_CPUTimeBeginFrame = 0.0f;
    float m_CPUTimeUpdateTileMappings = 0.0f;
    float m_CPUTimeResolve = 0.0f;

    float m_TileTimeoutSeconds = 1.0f;

private:
    void AllocateHeap(uint32_t& heapId);
};
#define g_TextureFeedbackManager g_Graphic.m_TextureFeedbackManager
