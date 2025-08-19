#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"
#include "extern/nvidia/RTXTS-TTM/include/rtxts-ttm/TiledTextureManager.h"

#include "Visual.h"

struct FeedbackTextureTileInfo
{
    uint32_t m_Mip;
    uint32_t m_XInTexels;
    uint32_t m_YInTexels;
    uint32_t m_WidthInTexels;
    uint32_t m_HeightInTexels;
};

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
    struct TileUpload
    {
        uint32_t m_TextureIdx;
        FeedbackTextureTileInfo m_TileInfo;
    };

    uint32_t AllocateHeap();
    void ReleaseHeap(uint32_t heapId);
    void UploadTile(nvrhi::CommandListHandle commandList, uint32_t destTextureIdx, const FeedbackTextureTileInfo& tile);

    std::unique_ptr<rtxts::TiledTextureManager> m_TiledTextureManager;
    std::mutex m_TiledTextureManagerLock;

    uint32_t m_HeapSizeInBytes;

    std::vector<uint32_t> m_TexturesToProcessThisFrame;
    std::vector<uint32_t> m_TexturesToReadback[2];
    std::vector<nvrhi::HeapHandle> m_Heaps;
    std::vector<nvrhi::BufferHandle> m_Buffers;
    std::vector<uint32_t> m_FreeHeapIDs;

    std::vector<TileUpload> m_TileUploads;

    std::vector<std::byte> m_UploadTileScratchBuffer;

    uint32_t m_NumHeaps = 0;
    
    int m_NumFeedbackTexturesToResolvePerFrame = 10;
    uint32_t m_ResolveFeedbackTexturesCounter = 0;
};
#define g_TextureFeedbackManager g_Graphic.m_TextureFeedbackManager
