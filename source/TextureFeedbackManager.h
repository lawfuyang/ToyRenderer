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
    void PrepareTexturesToProcessThisFrame();
    void BeginFrame();
    void EndFrame();

    void AddTexture(Texture& texture, const rtxts::TiledTextureDesc& tiledTextureDesc, rtxts::TextureDesc& feedbackDesc, rtxts::TextureDesc& minMipDesc);
    const std::vector<rtxts::TileCoord>& GetTileCoordinates(uint32_t tiledtextureID) const { return m_TiledTextureManager->GetTileCoordinates(tiledtextureID); }

private:
    struct UpdateTextureTileMappingsArgs
    {
        uint32_t m_TextureIdx;
        std::vector<nvrhi::TiledTextureCoordinate> m_TiledTextureCoordinates;
        std::vector<nvrhi::TiledTextureRegion> m_TiledTextureRegions;
        std::vector<uint64_t> m_ByteOffsets;
        uint32_t m_HeapID;
    };

    struct MipIORequest
    {
        uint32_t m_TextureIdx;
        uint32_t m_Mip;
        std::vector<FeedbackTextureTileInfo> m_DeferredTileInfosToUpload;
        std::vector<UpdateTextureTileMappingsArgs> m_DeferredTileMappings;
    };

    uint32_t AllocateHeap();
    void ReleaseHeap(uint32_t heapId);
    void UploadTile(nvrhi::CommandListHandle commandList, uint32_t destTextureIdx, const FeedbackTextureTileInfo& tile);

    void AsyncIOThreadFunc();
    std::thread m_AsyncIOThread;
    bool m_bShutDownAsyncIOThread = false;

    std::unique_ptr<rtxts::TiledTextureManager> m_TiledTextureManager;
    std::mutex m_TiledTextureManagerLock;

    uint32_t m_HeapSizeInBytes;

    std::vector<uint32_t> m_TexturesToProcessThisFrame;
    std::vector<uint32_t> m_TexturesToReadback[2];
    std::vector<nvrhi::HeapHandle> m_Heaps;
    std::vector<nvrhi::BufferHandle> m_Buffers;
    std::vector<uint32_t> m_FreeHeapIDs;

    std::vector<MipIORequest> m_MipIORequests;
    std::mutex m_MipIORequestsLock;

    std::vector<MipIORequest> m_DeferredTilesToMapAndUpload;
    std::mutex m_DeferredTilesToMapAndUploadLock;

    std::vector<std::byte> m_UploadTileScratchBuffer;

    bool m_bCompactMemory = true;
    int m_MaxTilesUploadPerFrame = 256;
    uint32_t m_NumHeaps = 0;
    
    int m_NumFeedbackTexturesToResolvePerFrame = 10;
    uint32_t m_ResolveFeedbackTexturesCounter = 0;
};
#define g_TextureFeedbackManager g_Graphic.m_TextureFeedbackManager
