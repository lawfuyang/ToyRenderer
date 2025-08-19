#include "TextureFeedbackManager.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "TextureLoading.h"

void TextureFeedbackManager::AddTexture(Texture& texture, const rtxts::TiledTextureDesc& tiledTextureDesc, rtxts::TextureDesc& feedbackDesc, rtxts::TextureDesc& minMipDesc)
{
    AUTO_LOCK(m_TiledTextureManagerLock);
    m_TiledTextureManager->AddTiledTexture(tiledTextureDesc, texture.m_TiledTextureID);
    feedbackDesc = m_TiledTextureManager->GetTextureDesc(texture.m_TiledTextureID, rtxts::eFeedbackTexture);
    minMipDesc = m_TiledTextureManager->GetTextureDesc(texture.m_TiledTextureID, rtxts::eMinMipTexture);
}

void TextureFeedbackManager::Initialize()
{
    m_TiledTextureManager = std::unique_ptr<rtxts::TiledTextureManager>{ rtxts::CreateTiledTextureManager(rtxts::TiledTextureManagerDesc{}) };
    m_TiledTextureManager->SetConfig(rtxts::TiledTextureManagerConfig{ 0 }); // no extra standby tiles

    m_HeapSizeInBytes = rtxts::TiledTextureManagerDesc{}.heapTilesCapacity * GraphicConstants::kTiledResourceSizeInBytes;

    m_UploadTileScratchBuffer.resize(GraphicConstants::kTiledResourceSizeInBytes);
}

void TextureFeedbackManager::Shutdown()
{
    m_TiledTextureManager.reset();
}

void TextureFeedbackManager::UpdateIMGUI()
{
    const rtxts::Statistics statistics = m_TiledTextureManager->GetStatistics();

    ImGui::Text("Tiles Total: %u (%.0f MB)", statistics.totalTilesNum, BYTES_TO_MB(statistics.totalTilesNum * GraphicConstants::kTiledResourceSizeInBytes));
    ImGui::Text("Tiles Allocated: %u (%.0f MB)", statistics.allocatedTilesNum, BYTES_TO_MB(statistics.allocatedTilesNum * GraphicConstants::kTiledResourceSizeInBytes));
    ImGui::Text("Heaps: %u (%.2f MB)", m_NumHeaps, BYTES_TO_MB(m_NumHeaps * m_HeapSizeInBytes));
    ImGui::Text("Heap Free Tiles: %d (%.0f MB)", statistics.heapFreeTilesNum, BYTES_TO_MB(statistics.heapFreeTilesNum * GraphicConstants::kTiledResourceSizeInBytes));

    ImGui::SliderInt("Feedback Textures to Resolve Per Frame", &m_NumFeedbackTexturesToResolvePerFrame, 1, 32);
    ImGui::Checkbox("Write Sampler Feedback", &g_Scene->m_bWriteSamplerFeedback);
}

void TextureFeedbackManager::PrepareTexturesToProcessThisFrame()
{
    if (g_Graphic.m_Textures.empty() || !g_Scene->m_bEnableTextureStreaming)
    {
        return;
    }

    PROFILE_FUNCTION();

    m_TexturesToProcessThisFrame.clear();
    for (uint32_t i = 0; i < m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        const uint32_t textureIdx = (m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();

        if (!m_TexturesToProcessThisFrame.empty() && m_TexturesToProcessThisFrame[0] == textureIdx)
        {
            break;
        }

        Texture& texture = g_Graphic.m_Textures.at(textureIdx);
        if (texture.m_TiledTextureID == UINT_MAX)
        {
            continue; // not a tiled texture
        }

        m_TexturesToProcessThisFrame.push_back(textureIdx);
    }
}

void TextureFeedbackManager::BeginFrame()
{
    if (g_Graphic.m_Textures.empty() || !g_Scene->m_bEnableTextureStreaming)
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMANDLIST_IMMEDIATE_EXECUTE(commandList, __FUNCTION__);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    // Begin frame, readback feedback
    std::vector<uint32_t>& texturesToReadback = m_TexturesToReadback[g_Graphic.m_FrameCounter % 2];
    {
        PROFILE_SCOPED("Readback Feedback Textures");

        for (uint32_t textureIdx : texturesToReadback)
        {
            Texture& texture = g_Graphic.m_Textures.at(textureIdx);
            nvrhi::BufferHandle resolveBuffer = texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2];

            void* pReadbackData = device->mapBuffer(resolveBuffer, nvrhi::CpuAccessMode::Read);

            rtxts::SamplerFeedbackDesc samplerFeedbackDesc;
            samplerFeedbackDesc.pMinMipData = (uint8_t*)pReadbackData;
            m_TiledTextureManager->UpdateWithSamplerFeedback(texture.m_TiledTextureID, samplerFeedbackDesc, 0.0f, 0.0f);

            device->unmapBuffer(resolveBuffer);

            // TODO: call 'MatchPrimaryTexture' for Material texture sets (albedo, normal, ORM)
        }
    }

    // get textures to process this frame
    {
        PROFILE_SCOPED("Clear Sampler Feedback Textures");
        
        texturesToReadback.clear();
        for (uint32_t textureIdx : m_TexturesToProcessThisFrame)
        {
            Texture& texture = g_Graphic.m_Textures.at(textureIdx);
            commandList->clearSamplerFeedbackTexture(texture.m_SamplerFeedbackTextureHandle);
            texturesToReadback.push_back(textureIdx);
        }
    }

    {
        PROFILE_SCOPED("Trim Standby Tiles");
        m_TiledTextureManager->TrimStandbyTiles();
    }

    {
        PROFILE_SCOPED("Add/Release Heaps");

        // Now check how many heaps the tiled texture manager needs
        const uint32_t numRequiredHeaps = m_TiledTextureManager->GetNumDesiredHeaps();
        if (numRequiredHeaps > m_NumHeaps)
        {
            while (m_NumHeaps < numRequiredHeaps)
            {
                const uint32_t heapID = AllocateHeap();
                m_TiledTextureManager->AddHeap(heapID);
            }
        }
        else
        {
            std::vector<uint32_t> emptyHeaps;
            m_TiledTextureManager->GetEmptyHeaps(emptyHeaps);
            for (uint32_t heapID : emptyHeaps)
            {
                m_TiledTextureManager->RemoveHeap(heapID);
                ReleaseHeap(heapID);
            }
        }
    }

    // Now let the tiled texture manager allocate
    {
        PROFILE_SCOPED("Allocate Requested Tiles");
        m_TiledTextureManager->AllocateRequestedTiles();
    }

    struct TextureAndTiles
    {
        const uint32_t m_TextureIdx;
        std::vector<uint32_t> m_TileIndices;
    };
    std::vector<TextureAndTiles> textureAndTilesToMap;

    std::vector<uint32_t> texturesWithDirtyMinMipTextures;

    // Get tiles to unmap and map from the tiled texture manager
    {
        PROFILE_SCOPED("Get Tiles to Map & Unmap");

        // TODO: The current code does not merge unmapping and mapping tiles for the same textures. It would be more optimal.
        std::vector<uint32_t> tilesToMap;
        std::vector<uint32_t> tilesToUnmap;
        for (uint32_t i = 0; i < g_Graphic.m_Textures.size(); ++i)
        {
            Texture& texture = g_Graphic.m_Textures[i];
            if (texture.m_TiledTextureID == UINT_MAX)
            {
                continue; // not a tiled texture
            }

            m_TiledTextureManager->GetTilesToMap(texture.m_TiledTextureID, tilesToMap);
            m_TiledTextureManager->GetTilesToUnmap(texture.m_TiledTextureID, tilesToUnmap);

            const std::vector<rtxts::TileCoord>& tilesCoordinates = m_TiledTextureManager->GetTileCoordinates(texture.m_TiledTextureID);

            if (!tilesToUnmap.empty() || !tilesToMap.empty())
            {
                texturesWithDirtyMinMipTextures.push_back(i);
            }

            if (!tilesToUnmap.empty())
            {
                nvrhi::TiledTextureRegion tiledTextureRegion;
                tiledTextureRegion.tilesNum = 1;

                nvrhi::TextureTilesMapping textureTilesMapping;
                textureTilesMapping.numTextureRegions = tilesToUnmap.size();

                std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates;
                tiledTextureCoordinates.resize(tilesToUnmap.size());

                std::vector<nvrhi::TiledTextureRegion> tiledTextureRegions;
                tiledTextureRegions.resize(tilesToUnmap.size(), tiledTextureRegion);

                textureTilesMapping.tiledTextureCoordinates = tiledTextureCoordinates.data();
                textureTilesMapping.tiledTextureRegions = tiledTextureRegions.data();
                textureTilesMapping.heap = nullptr; // nullptr for heap == unmap

                uint32_t tilesProcessedNum = 0;
                for (uint32_t tileIndex : tilesToUnmap)
                {
                    // Process only unpacked tiles
                    nvrhi::TiledTextureCoordinate& tiledTextureCoordinate = tiledTextureCoordinates[tilesProcessedNum];
                    tiledTextureCoordinate.mipLevel = tilesCoordinates[tileIndex].mipLevel;
                    tiledTextureCoordinate.arrayLevel = 0;
                    tiledTextureCoordinate.x = tilesCoordinates[tileIndex].x;
                    tiledTextureCoordinate.y = tilesCoordinates[tileIndex].y;
                    tiledTextureCoordinate.z = 0;

                    tilesProcessedNum++;

                    if (!texture.IsTilePacked(tileIndex))
                    {
                        TextureMipData& mipData = texture.m_TextureMipDatas.at(tilesCoordinates[tileIndex].mipLevel);
                        assert(tileIndex >= mipData.m_FirstTileIndex);
                        const uint32_t mipTileIndex = tileIndex - mipData.m_FirstTileIndex;
                        mipData.m_ResidencyBits.ClearBit(mipTileIndex);
                    }
                }

                device->updateTextureTileMappings(texture.m_NVRHITextureHandle, &textureTilesMapping, 1);
            }

            if (!tilesToMap.empty())
            {
                for (uint32_t tileIndex : tilesToMap)
                {
                    if (texture.IsTilePacked(tileIndex))
                    {
                        continue; // skip packed tiles
                    }

                    TextureMipData& mipData = texture.m_TextureMipDatas.at(tilesCoordinates[tileIndex].mipLevel);
                    assert(tileIndex >= mipData.m_FirstTileIndex);
                    const uint32_t mipTileIndex = tileIndex - mipData.m_FirstTileIndex;
                    mipData.m_ResidencyBits.SetBit(mipTileIndex);
                }

                textureAndTilesToMap.push_back({ i, std::move(tilesToMap) });
            }

            // clear mip data from system memory when entire mip is streamed out
            // TODO: try enabling this after implementing DirectStorage, else the stuttering caused by I/O is really bad
            if constexpr (false)
            {
                for (uint32_t i = 0; i < texture.m_TextureMipDatas.size(); ++i)
                {
                    if (i >= texture.m_PackedMipDesc.numStandardMips)
                    {
                        break;
                    }

                    TextureMipData& mipData = texture.m_TextureMipDatas[i];
                    if (mipData.m_ResidencyBits.IsEmpty())
                    {
                        // forcefully remove mip data from system memory. "clear" is not enough
                        std::vector<std::byte> emptyVec;
                        mipData.m_Data.swap(emptyVec);

                        mipData.m_bDataReady = false;
                    }
                }
            }
        }
    }

    {
        PROFILE_SCOPED("Defragment Tiles");

        const uint32_t kNumTilesToDefragment = 16;
        m_TiledTextureManager->DefragmentTiles(kNumTilesToDefragment);
    }

    std::vector<FeedbackTextureTileInfo> tiles;
    for (TextureAndTiles& texUpdate : textureAndTilesToMap)
    {
        PROFILE_SCOPED("Update Tile Mappings");

        Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);

        m_TiledTextureManager->UpdateTilesMapping(texture.m_TiledTextureID, texUpdate.m_TileIndices);

        const std::vector<rtxts::TileCoord>& tilesCoordinates = m_TiledTextureManager->GetTileCoordinates(texture.m_TiledTextureID);
        const std::vector<rtxts::TileAllocation>& tilesAllocations = m_TiledTextureManager->GetTileAllocations(texture.m_TiledTextureID);

        std::unordered_map<uint32_t, std::vector<uint32_t>> heapTilesMapping;
        for (uint32_t tileIndex : texUpdate.m_TileIndices)
        {
            heapTilesMapping[tilesAllocations[tileIndex].heapId].push_back(tileIndex);
        }

        for (const auto& [heapId, heapTiles] : heapTilesMapping)
        {
            std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates;
            std::vector<nvrhi::TiledTextureRegion> tiledTextureRegions;
            std::vector<uint64_t> byteOffsets;

            for (uint32_t tileIndex : heapTiles)
            {
                nvrhi::TiledTextureCoordinate& tiledTextureCoordinate = tiledTextureCoordinates.emplace_back();
                tiledTextureCoordinate.mipLevel = tilesCoordinates[tileIndex].mipLevel;
                tiledTextureCoordinate.x = tilesCoordinates[tileIndex].x;
                tiledTextureCoordinate.y = tilesCoordinates[tileIndex].y;
                tiledTextureCoordinate.z = 0;

                nvrhi::TiledTextureRegion& tiledTextureRegion = tiledTextureRegions.emplace_back();
                tiledTextureRegion.tilesNum = 1;

                uint64_t& byteOffset = byteOffsets.emplace_back();
                byteOffset = tilesAllocations[tileIndex].heapTileIndex * GraphicConstants::kTiledResourceSizeInBytes;
            }

            nvrhi::TextureTilesMapping textureTilesMapping;
            textureTilesMapping.numTextureRegions = tiledTextureCoordinates.size();
            textureTilesMapping.tiledTextureCoordinates = tiledTextureCoordinates.data();
            textureTilesMapping.tiledTextureRegions = tiledTextureRegions.data();
            textureTilesMapping.byteOffsets = byteOffsets.data();
            textureTilesMapping.heap = m_Heaps.at(heapId);

            device->updateTextureTileMappings(texture.m_NVRHITextureHandle, &textureTilesMapping, 1);
        }

        for (uint32_t tileIndex : texUpdate.m_TileIndices)
        {
            tiles.clear();
            texture.GetTileInfo(tileIndex, tiles);

            for (FeedbackTextureTileInfo& tile : tiles)
            {
                TextureMipData& mipData = texture.m_TextureMipDatas.at(tile.m_Mip);
                if (texture.IsTilePacked(tileIndex))
                {
                    PROFILE_SCOPED("Upload Packed Tile");

                    // packed mips are persistently loaded in memory. immediately upload
                    commandList->writeTexture(texture.m_NVRHITextureHandle, 0, tile.m_Mip, mipData.m_Data.data(), mipData.m_RowPitch);
                }
                else
                {
                    if (!mipData.m_bDataReady)
                    {
                        ScopedFile f{ texture.m_ImageFilePath, "rb" };
                        ReadDDSMipData(texture, f, tile.m_Mip);
                    }

                    UploadTile(commandList, texUpdate.m_TextureIdx, tile);
                }
            }
        }
    }

    // Write min mip data
    std::vector<uint8_t> minMipData;
    for (uint32_t i : texturesWithDirtyMinMipTextures)
    {
        Texture& texture = g_Graphic.m_Textures.at(i);

        const nvrhi::TextureDesc& minMipTexDesc = texture.m_MinMipTextureHandle->getDesc();

        minMipData.resize(minMipTexDesc.width* minMipTexDesc.height);
        m_TiledTextureManager->WriteMinMipData(texture.m_TiledTextureID, minMipData.data());

        const uint32_t rowPitch = minMipTexDesc.width;
        commandList->writeTexture(texture.m_MinMipTextureHandle, 0, 0, minMipData.data(), rowPitch);
    }
}

void TextureFeedbackManager::EndFrame()
{
    if (g_Graphic.m_Textures.empty() || !g_Scene->m_bEnableTextureStreaming)
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    for (uint32_t i : m_TexturesToProcessThisFrame)
    {
        Texture& texture = g_Graphic.m_Textures.at(i);
        commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
    }

    m_ResolveFeedbackTexturesCounter = (m_ResolveFeedbackTexturesCounter + m_NumFeedbackTexturesToResolvePerFrame) % g_Graphic.m_Textures.size();
}

uint32_t TextureFeedbackManager::AllocateHeap()
{
    PROFILE_FUNCTION();
    
    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::HeapDesc heapDesc;
    heapDesc.capacity = m_HeapSizeInBytes;
    heapDesc.type = nvrhi::HeapType::DeviceLocal;

    // TODO: Calling createHeap should ideally be called asynchronously to offload the critical path
    nvrhi::HeapHandle heap = device->createHeap(heapDesc);

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = m_HeapSizeInBytes;
    bufferDesc.isVirtual = true;
    bufferDesc.initialState = nvrhi::ResourceStates::CopySource;
    bufferDesc.keepInitialState = true;
    nvrhi::BufferHandle buffer = device->createBuffer(bufferDesc);

    device->bindBufferMemory(buffer, heap, 0);

    uint32_t heapID;
    if (m_FreeHeapIDs.empty())
    {
        heapID = (uint32_t)m_Heaps.size();
        m_Heaps.push_back(heap);
        m_Buffers.push_back(buffer);
    }
    else
    {
        heapID = m_FreeHeapIDs.back();
        m_FreeHeapIDs.pop_back();
        m_Heaps[heapID] = heap;
        m_Buffers[heapID] = buffer;
    }

    ++m_NumHeaps;

    return heapID;
}

void TextureFeedbackManager::ReleaseHeap(uint32_t heapID)
{
    m_FreeHeapIDs.push_back(heapID);
    m_NumHeaps--;

    // NOTE: dont free heaps & buffers when entire heap's tiles are unmapped... else potential device TDR. not ideal, but i dont care for now
    //       extra VRAM cost is not significant, as we'll need to allocate for worst case view in scene anyway
}

void TextureFeedbackManager::UploadTile(nvrhi::CommandListHandle commandList, uint32_t destTextureIdx, const FeedbackTextureTileInfo& tile)
{
    PROFILE_FUNCTION();

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
    const Texture& destTexture = g_Graphic.m_Textures.at(destTextureIdx);
    const TextureMipData& mipData = destTexture.m_TextureMipDatas.at(tile.m_Mip);

    // Compute pitches and offsets in 4x4 blocks
    // Note: The "tile" being copied here might be smaller than a tiled resource tile, for example non-pow2 textures
    const uint32_t blockSize = nvrhi::getFormatInfo(destTexture.m_NVRHITextureHandle->getDesc().format).blockSize;
    const uint32_t tileBlocksWidth = tile.m_WidthInTexels / blockSize;
    const uint32_t tileBlocksHeight = tile.m_HeightInTexels / blockSize;
    const uint32_t shapeBlocksWidth = destTexture.m_TileShape.widthInTexels / blockSize;
    const uint32_t shapeBlocksHeight = destTexture.m_TileShape.heightInTexels / blockSize;
    const uint32_t bytesPerBlock = GraphicConstants::kTiledResourceSizeInBytes / (shapeBlocksWidth * shapeBlocksHeight);
    const uint32_t sourceBlockX = tile.m_XInTexels / blockSize;
    const uint32_t sourceBlockY = tile.m_YInTexels / blockSize;
    const uint32_t rowPitchTile = tileBlocksWidth * bytesPerBlock;

    assert(m_UploadTileScratchBuffer.size() >= tileBlocksWidth * tileBlocksHeight * bytesPerBlock);

    for (uint32_t blockRow = 0; blockRow < tileBlocksHeight; blockRow++)
    {
        const int32_t readOffset = (sourceBlockY + blockRow) * mipData.m_RowPitch + sourceBlockX * bytesPerBlock;
        const int32_t writeOffset = blockRow * rowPitchTile;
        memcpy(m_UploadTileScratchBuffer.data() + writeOffset, mipData.m_Data.data() + readOffset, rowPitchTile);
    }

    nvrhi::TextureSlice destSlice;
    destSlice.x = tile.m_XInTexels;
    destSlice.y = tile.m_YInTexels;
    destSlice.z = 0;
    destSlice.width = tile.m_WidthInTexels;
    destSlice.height = tile.m_HeightInTexels;
    destSlice.depth = 1;
    destSlice.mipLevel = tile.m_Mip;

    PROFILE_SCOPED("writeTexture");
    commandList->writeTexture(destTexture.m_NVRHITextureHandle, destSlice, m_UploadTileScratchBuffer.data(), rowPitchTile);
}
