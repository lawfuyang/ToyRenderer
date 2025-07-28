#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvidia/RTXTS-TTM/include/rtxts-ttm/TiledTextureManager.h"

#include "Visual.h"

class TextureFeedbackManager
{
public:
    SingletonFunctionsSimple(TextureFeedbackManager);

    void Initialize();
    void Shutdown();
    void UpdateIMGUI();
    void BeginFrame();
    void EndFrame();

    std::unique_ptr<rtxts::TiledTextureManager> m_TiledTextureManager;
    std::mutex m_TiledTextureManagerLock;

private:
    struct MipIORequest
    {
        uint32_t m_TextureIdx;
        FeedbackTextureTileInfo m_TileInfo;
    };

    uint32_t AllocateHeap();
    void ReleaseHeap(uint32_t heapId);

    void AsyncIOThreadFunc();
    std::thread m_AsyncIOThread;
    bool m_bShutDownAsyncIOThread = false;

    std::vector<uint32_t> m_TexturesToReadback[2];
    std::vector<nvrhi::HeapHandle> m_Heaps;
    std::vector<nvrhi::BufferHandle> m_Buffers;
    std::vector<uint32_t> m_FreeHeapIDs;

    std::vector<MipIORequest> m_MipIORequests;
    std::mutex m_MipIORequestsLock;

    std::vector<MipIORequest> m_DeferredTilesToUpload;
    std::mutex m_DeferredTilesToUploadLock;

    bool m_bCompactMemory = true;
    int m_NumExtraStandbyTiles = 0; // TODO: set to default of 1000
    float m_TileTimeoutSeconds = 0.0f; // TODO: set to 1.0f
    int m_MaxTilesUploadPerFrame = 256;
    uint32_t m_NumHeaps = 0;
    uint64_t m_HeapAllocationInBytes = 0;
    
    int m_NumFeedbackTexturesToResolvePerFrame = 10;
    uint32_t m_ResolveFeedbackTexturesCounter = 0;
};
#define g_TextureFeedbackManager g_Graphic.m_TextureFeedbackManager
