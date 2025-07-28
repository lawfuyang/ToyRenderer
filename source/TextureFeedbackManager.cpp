#include "TextureFeedbackManager.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

static void UploadTile(nvrhi::CommandListHandle commandList, uint32_t destTextureIdx, const FeedbackTextureTileInfo& tile)
{
    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
    const Texture& destTexture = g_Graphic.m_Textures.at(destTextureIdx);
    const TextureMipData& mipData = destTexture.m_TextureMipDatas[tile.m_Mip];

    nvrhi::TextureDesc stagingTextureDesc;
    stagingTextureDesc.width = tile.m_WidthInTexels;
    stagingTextureDesc.height = tile.m_HeightInTexels;
    stagingTextureDesc.format = destTexture.m_NVRHITextureHandle->getDesc().format;
    nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(stagingTextureDesc, nvrhi::CpuAccessMode::Write);

    size_t rowPitch;
    std::byte* mappedData = (std::byte*)device->mapStagingTexture(stagingTexture, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Write, &rowPitch);

    // Compute pitches and offsets in 4x4 blocks
    // Note: The "tile" being copied here might be smaller than a tiled resource tile, for example non-pow2 textures
    const uint32_t blockSize = nvrhi::getFormatInfo(destTexture.m_NVRHITextureHandle->getDesc().format).blockSize;
    const uint32_t tileBlocksWidth = tile.m_WidthInTexels / blockSize;
    const uint32_t tileBlocksHeight = tile.m_HeightInTexels / blockSize;
    const uint32_t shapeBlocksWidth = destTexture.m_TileShape.widthInTexels / blockSize;
    const uint32_t shapeBlocksHeight = destTexture.m_TileShape.heightInTexels / blockSize;
    const uint32_t bytesPerBlock = g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes() / (shapeBlocksWidth * shapeBlocksHeight);
    const uint32_t sourceBlockX = tile.m_XInTexels / blockSize;
    const uint32_t sourceBlockY = tile.m_YInTexels / blockSize;
    const uint32_t rowPitchTile = tileBlocksWidth * bytesPerBlock;

    assert(rowPitch == rowPitchTile);

    for (uint32_t blockRow = 0; blockRow < tileBlocksHeight; blockRow++)
    {
        const int32_t readOffset = (sourceBlockY + blockRow) * mipData.m_RowPitch + sourceBlockX * bytesPerBlock;
        const int32_t writeOffset = blockRow * rowPitchTile;
        memcpy(mappedData + writeOffset, mipData.m_Data.data() + readOffset, rowPitchTile);
    }

    device->unmapStagingTexture(stagingTexture);

    nvrhi::TextureSlice destSlice;
    destSlice.x = tile.m_XInTexels;
    destSlice.y = tile.m_YInTexels;
    destSlice.z = 0;
    destSlice.width = tile.m_WidthInTexels;
    destSlice.height = tile.m_HeightInTexels;
    destSlice.depth = 1;
    destSlice.mipLevel = tile.m_Mip;

    commandList->copyTexture(destTexture.m_NVRHITextureHandle, destSlice, stagingTexture, nvrhi::TextureSlice{});
}

void TextureFeedbackManager::AsyncIOThreadFunc()
{
    auto ProcessAsyncIOResults = [this]()
    {
        SDL_AsyncIOOutcome asyncIOOutcome{};
        while (SDL_GetAsyncIOResult(g_Engine.m_AsyncIOQueue, &asyncIOOutcome))
        {
            if (asyncIOOutcome.type == SDL_ASYNCIO_TASK_CLOSE)
            {
                // this is a close request, we can ignore it
                continue;
            }

            PROFILE_SCOPED("Process Async IO Result");

            assert(asyncIOOutcome.type == SDL_ASYNCIO_TASK_READ);
            assert(asyncIOOutcome.result == SDL_ASYNCIO_COMPLETE);
            assert(asyncIOOutcome.userdata);

            MipIORequest* inFlightRequest = static_cast<MipIORequest*>(asyncIOOutcome.userdata);
            ON_EXIT_SCOPE_LAMBDA([inFlightRequest] { delete inFlightRequest; });

            MipIORequest deferredTileToUpload;
            memcpy(&deferredTileToUpload, inFlightRequest, sizeof(MipIORequest));

            AUTO_LOCK(m_DeferredTilesToUploadLock);
            m_DeferredTilesToUpload.push_back(deferredTileToUpload);
        }
    };

    while (!m_bShutDownAsyncIOThread)
    {
        ProcessAsyncIOResults();

        std::vector<MipIORequest> mipIORequests;
        {
            AUTO_LOCK(m_MipIORequestsLock);
            mipIORequests = std::move(m_MipIORequests);
        }

        for (const MipIORequest& request : mipIORequests)
        {
            Texture& texture = g_Graphic.m_Textures.at(request.m_TextureIdx);

            assert(texture.IsValid());
            assert(!texture.m_ImageFilePath.empty());

            TextureMipData& textureMipData = texture.m_TextureMipDatas[request.m_TileInfo.m_Mip];
            assert(textureMipData.IsValid());
            assert(!textureMipData.m_Data.empty()); // caller must alloc memory before submitting the request

            SDL_AsyncIO* asyncIO = SDL_AsyncIOFromFile(texture.m_ImageFilePath.c_str(), "r");
            SDL_CALL(asyncIO);

            MipIORequest* inFlightRequest = new MipIORequest;
            memcpy(inFlightRequest, &request, sizeof(MipIORequest));

            SDL_CALL(SDL_ReadAsyncIO(asyncIO, textureMipData.m_Data.data(), textureMipData.m_DataOffset, textureMipData.m_NumBytes, g_Engine.m_AsyncIOQueue, (void*)inFlightRequest));

            // according to the doc, we can close the async IO handle after the read request is submitted
            SDL_CALL(SDL_CloseAsyncIO(asyncIO, false, g_Engine.m_AsyncIOQueue, nullptr));

            // immediately process & discard the SDL_ASYNCIO_TASK_CLOSE result
            ProcessAsyncIOResults();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // yield to avoid busy waiting
    }
}

void TextureFeedbackManager::Initialize()
{
    m_TiledTextureManager = std::unique_ptr<rtxts::TiledTextureManager>{ rtxts::CreateTiledTextureManager(rtxts::TiledTextureManagerDesc{}) };

    m_AsyncIOThread = std::thread(&TextureFeedbackManager::AsyncIOThreadFunc, this);
}

void TextureFeedbackManager::Shutdown()
{
    m_TiledTextureManager.reset();

    m_bShutDownAsyncIOThread = true;
    m_AsyncIOThread.join();
}

void TextureFeedbackManager::UpdateIMGUI()
{
    const rtxts::Statistics statistics = m_TiledTextureManager->GetStatistics();

    ImGui::Text("Tiles Total: %u (%.0f MB)", statistics.totalTilesNum, BYTES_TO_MB(statistics.totalTilesNum * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes()));
    ImGui::Text("Tiles Allocated: %u (%.0f MB)", statistics.allocatedTilesNum, BYTES_TO_MB(statistics.allocatedTilesNum * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes()));
    ImGui::Text("Tiles Standby: %u (%.0f MB)", statistics.standbyTilesNum, BYTES_TO_MB(statistics.standbyTilesNum * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes()));
    ImGui::Text("Heap allocation: %.2f MB", BYTES_TO_MB(m_HeapAllocationInBytes));
    ImGui::Text("Heap Free Tiles: %d (%.0f MB)", statistics.heapFreeTilesNum, BYTES_TO_MB(statistics.heapFreeTilesNum * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes()));

    ImGui::SliderInt("Extra Standby Tiles", &m_NumExtraStandbyTiles, 0, 2000);
    ImGui::SliderInt("Feedback Textures to Resolve Per Frame", &m_NumFeedbackTexturesToResolvePerFrame, 10, g_Graphic.m_Textures.size());
    ImGui::SliderInt("Max Tiles Upload Per Frame", &m_MaxTilesUploadPerFrame, 16, 1024);
    ImGui::SliderFloat("Tile Timeout (seconds)", &m_TileTimeoutSeconds, 0.0f, 3.0f);
    ImGui::Checkbox("Compact Memory", &m_bCompactMemory);
}

void TextureFeedbackManager::BeginFrame()
{
    if (g_Graphic.m_Textures.empty() || !g_Scene->m_bEnableTextureStreaming)
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    g_Graphic.BeginCommandList(commandList, __FUNCTION__);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    m_TiledTextureManager->SetConfig(rtxts::TiledTextureManagerConfig{ (uint32_t)m_NumExtraStandbyTiles });

    // Begin frame, readback feedback
    std::vector<uint32_t>& texturesToReadback = m_TexturesToReadback[g_Graphic.m_FrameCounter % 2];
    for (uint32_t textureIdx : texturesToReadback)
    {
        Texture& texture = g_Graphic.m_Textures.at(textureIdx);
        nvrhi::BufferHandle resolveBuffer = texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2];

        void* pReadbackData = device->mapBuffer(resolveBuffer, nvrhi::CpuAccessMode::Read);

        rtxts::SamplerFeedbackDesc samplerFeedbackDesc;
        samplerFeedbackDesc.pMinMipData = (uint8_t*)pReadbackData;
        m_TiledTextureManager->UpdateWithSamplerFeedback(texture.m_TiledTextureID, samplerFeedbackDesc, g_Graphic.m_GraphicTimer.GetElapsedSeconds(), m_TileTimeoutSeconds);

        device->unmapBuffer(resolveBuffer);

        // TODO: call 'MatchPrimaryTexture' if necessary, whatever it means?
    }
    texturesToReadback.clear();

    // TODO: delete this & enable frame slicing
    m_NumFeedbackTexturesToResolvePerFrame = g_Graphic.m_Textures.size();

    // Collect textures to read back
    {
        const uint32_t startIdx = m_ResolveFeedbackTexturesCounter % g_Graphic.m_Textures.size();
        for (uint32_t i = 0; i < m_NumFeedbackTexturesToResolvePerFrame; ++i)
        {
            if ((i > 0) && (i == startIdx))
            {
                break;
            }

            const uint32_t textureIdx = (m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
            Texture& texture = g_Graphic.m_Textures[textureIdx];

            if (texture.m_TiledTextureID != UINT_MAX)
            {
                commandList->clearSamplerFeedbackTexture(texture.m_SamplerFeedbackTextureHandle);
                texturesToReadback.push_back(textureIdx);
            }
        }
    }

    if (m_bCompactMemory)
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
        else if (m_bCompactMemory)
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
    m_TiledTextureManager->AllocateRequestedTiles();

    struct FeedbackTextureUpdate
    {
        uint32_t m_TextureIdx = UINT_MAX;
        std::vector<uint32_t> m_TileIndices;
    };
    std::vector<FeedbackTextureUpdate> feedbackTextureUpdates;

    // Get tiles to unmap and map from the tiled texture manager
    // TODO: The current code does not merge unmapping and mapping tiles for the same textures. It would be more optimal.
    std::vector<uint32_t> tilesToMap;
    std::vector<uint32_t> tilesToUnmap;
    std::unordered_set<uint32_t> minMipDirtyTextures;
    for (uint32_t i = 0; i < g_Graphic.m_Textures.size(); ++i)
    {
        Texture& texture = g_Graphic.m_Textures[i];
        if (texture.m_TiledTextureID == UINT_MAX)
        {
            continue; // not a tiled texture
        }

        m_TiledTextureManager->GetTilesToMap(texture.m_TiledTextureID, tilesToMap);
        m_TiledTextureManager->GetTilesToUnmap(texture.m_TiledTextureID, tilesToUnmap);

        if (!tilesToUnmap.empty())
        {
            //LOG_DEBUG("Unmapping %u tiles for texture %u", (uint32_t)tilesToUnmap.size(), i);

            // TODO: keep track of mapped & unmapped tiles in Texture class, and free the TextureMipData memory when all tiles in mip gets unmapped
            const std::vector<rtxts::TileCoord>& tilesCoordinates = m_TiledTextureManager->GetTileCoordinates(texture.m_TiledTextureID);
            const uint32_t tileToUnmapNum = (uint32_t)tilesToUnmap.size();

            nvrhi::TiledTextureRegion tiledTextureRegion;
            tiledTextureRegion.tilesNum = 1;

            nvrhi::TextureTilesMapping textureTilesMapping;
            textureTilesMapping.numTextureRegions = tileToUnmapNum;

            std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates;
            tiledTextureCoordinates.resize(tileToUnmapNum);

            std::vector<nvrhi::TiledTextureRegion> tiledTextureRegions;
            tiledTextureRegions.resize(tileToUnmapNum, tiledTextureRegion);
            
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
            }

            device->updateTextureTileMappings(texture.m_NVRHITextureHandle, &textureTilesMapping, 1);

            minMipDirtyTextures.insert(i);
        }

        if (!tilesToMap.empty())
        {
            //LOG_DEBUG("Mapping %u tiles for texture %u", (uint32_t)tilesToMap.size(), i);

            FeedbackTextureUpdate& feedbackTextureUpdate = feedbackTextureUpdates.emplace_back();

            feedbackTextureUpdate.m_TextureIdx = i;
            for (uint32_t tileIndex : tilesToMap)
            {
                assert(std::find(feedbackTextureUpdate.m_TileIndices.begin(), feedbackTextureUpdate.m_TileIndices.end(), tileIndex) == feedbackTextureUpdate.m_TileIndices.end());
                feedbackTextureUpdate.m_TileIndices.push_back(tileIndex);
            }
        }
    }

    if (m_bCompactMemory)
    {
        PROFILE_SCOPED("Defragment Tiles");

        const uint32_t kNumTilesToDefragment = 16;
        m_TiledTextureManager->DefragmentTiles(kNumTilesToDefragment);
    }

    struct RequestedTile
    {
        const uint32_t m_TextureIdx;
        const uint32_t m_TileIndex;
    };
    std::vector<RequestedTile> requestedTiles;
    std::vector<RequestedTile> requestedPackedTiles;

    // Collect all tiles and store them in the queue
    for (FeedbackTextureUpdate& texUpdate : feedbackTextureUpdates)
    {
        for (uint32_t i = 0; i < texUpdate.m_TileIndices.size(); i++)
        {
            Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);
            const RequestedTile reqTile{ texUpdate.m_TextureIdx, texUpdate.m_TileIndices[i] };
            
            if (texture.IsTilePacked(reqTile.m_TileIndex))
            {
                requestedPackedTiles.push_back(reqTile);
            }
            else
            {
                requestedTiles.push_back(reqTile);
            }
        }
    }

    // Figure out which tiles to map and upload this frame
    std::vector<FeedbackTextureUpdate> tilesThisFrame;
    auto ScheduleTileForUpload = [&](const RequestedTile& reqTile)
        {
            // Find if we already have this texture in tilesThisFrame
            FeedbackTextureUpdate* pTexUpdate = nullptr;
            for (uint32_t t = 0; t < tilesThisFrame.size(); t++)
            {
                if (tilesThisFrame[t].m_TextureIdx == reqTile.m_TextureIdx)
                {
                    pTexUpdate = &tilesThisFrame[t];
                    break;
                }
            }

            if (pTexUpdate == nullptr)
            {
                // First time we see this texture this frame
                FeedbackTextureUpdate texUpdate;
                texUpdate.m_TextureIdx = reqTile.m_TextureIdx;
                tilesThisFrame.push_back(texUpdate);
                pTexUpdate = &tilesThisFrame.back();
            }

            pTexUpdate->m_TileIndices.push_back(reqTile.m_TileIndex);
        };

    // Upload all packed tiles this frame
    for (const RequestedTile& packedTile : requestedPackedTiles)
    {
        ScheduleTileForUpload(packedTile);
    }

    // Compute how many tiles we will upload this frame
    {
        m_MaxTilesUploadPerFrame = INT_MAX; // TODO: delete this and enable frame slicing
        const uint32_t countUpload = std::min<uint32_t>(requestedTiles.size(), m_MaxTilesUploadPerFrame);
        for (uint32_t i = 0; i < countUpload; i++)
        {
            ScheduleTileForUpload(requestedTiles[i]);
        }
    }

    {
        PROFILE_SCOPED("Update Tile Mappings");

        for (FeedbackTextureUpdate& texUpdate : tilesThisFrame)
        {
            Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);

            //LOG_DEBUG("Updating %d tiles for texture: %s", texUpdate.m_TileIndices.size(), texture.m_NVRHITextureHandle->getDesc().debugName.c_str());

            minMipDirtyTextures.insert(texUpdate.m_TextureIdx);

            m_TiledTextureManager->UpdateTilesMapping(texture.m_TiledTextureID, texUpdate.m_TileIndices);

            const std::vector<rtxts::TileCoord>& tilesCoordinates = m_TiledTextureManager->GetTileCoordinates(texture.m_TiledTextureID);
            const std::vector<rtxts::TileAllocation>& tilesAllocations = m_TiledTextureManager->GetTileAllocations(texture.m_TiledTextureID);

            std::unordered_map<nvrhi::HeapHandle, std::vector<uint32_t>> heapTilesMapping;
            for (uint32_t tileIndex : texUpdate.m_TileIndices)
            {
                nvrhi::HeapHandle heap = m_Heaps.at(tilesAllocations[tileIndex].heapId);
                heapTilesMapping[heap].push_back(tileIndex);
            }

            // Now loop heaps
            for (const auto& [heap, heapTiles] : heapTilesMapping)
            {
                std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates;
                std::vector<nvrhi::TiledTextureRegion> tiledTextureRegions;
                std::vector<uint64_t> byteOffsets;

                for (uint32_t i = 0; i < heapTiles.size(); i++)
                {
                    uint32_t tileIndex = heapTiles[i];

                    nvrhi::TiledTextureCoordinate tiledTextureCoordinate;
                    tiledTextureCoordinate.mipLevel = tilesCoordinates[tileIndex].mipLevel;
                    tiledTextureCoordinate.x = tilesCoordinates[tileIndex].x;
                    tiledTextureCoordinate.y = tilesCoordinates[tileIndex].y;
                    tiledTextureCoordinate.z = 0;
                    tiledTextureCoordinates.push_back(tiledTextureCoordinate);

                    nvrhi::TiledTextureRegion tiledTextureRegion;
                    tiledTextureRegion.tilesNum = 1;
                    tiledTextureRegions.push_back(tiledTextureRegion);

                    byteOffsets.push_back(tilesAllocations[tileIndex].heapTileIndex * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes());
                }

                nvrhi::TextureTilesMapping textureTilesMapping;
                textureTilesMapping.numTextureRegions = (uint32_t)tiledTextureCoordinates.size();
                textureTilesMapping.tiledTextureCoordinates = tiledTextureCoordinates.data();
                textureTilesMapping.tiledTextureRegions = tiledTextureRegions.data();
                textureTilesMapping.byteOffsets = byteOffsets.data();
                textureTilesMapping.heap = heap;

                device->updateTextureTileMappings(texture.m_NVRHITextureHandle, &textureTilesMapping, 1);
            }
        }

        if (!minMipDirtyTextures.empty())
        {
            PROFILE_SCOPED("Update Min Mip Textures");

            std::vector<uint8_t> minMipData;
            for (uint32_t textureIdx : minMipDirtyTextures)
            {
                const Texture& texture = g_Graphic.m_Textures.at(textureIdx);

                //LOG_DEBUG("Updating MinMip texture for texture: %s", texture.m_NVRHITextureHandle->getDesc().debugName.c_str());

                const nvrhi::TextureDesc& minMipTexDesc = texture.m_MinMipTextureHandle->getDesc();

                minMipData.resize(minMipTexDesc.width * minMipTexDesc.height);
                m_TiledTextureManager->WriteMinMipData(texture.m_TiledTextureID, minMipData.data());

                const uint32_t rowPitch = minMipTexDesc.width;
                commandList->writeTexture(texture.m_MinMipTextureHandle, 0, 0, minMipData.data(), rowPitch);
            }
        }
    }

    // Upload the tiles to the GPU and copy them into the resources
    if (!tilesThisFrame.empty())
    {
        PROFILE_SCOPED("Upload Tiles");

        std::vector<FeedbackTextureTileInfo> tiles;

        for (const FeedbackTextureUpdate& texUpdate : tilesThisFrame)
        {
            Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);

            for (uint32_t tileIndex : texUpdate.m_TileIndices)
            {
                tiles.clear();
                texture.GetTileInfo(tileIndex, tiles);

                for (const FeedbackTextureTileInfo& tile : tiles)
                {
                    TextureMipData& mipData = texture.m_TextureMipDatas[tile.m_Mip];
                    if (texture.IsTilePacked(tileIndex))
                    {
                        // packed mips are persistently loaded in memory. immediately upload
                        commandList->writeTexture(texture.m_NVRHITextureHandle, 0, tile.m_Mip, mipData.m_Data.data(), mipData.m_RowPitch);
                    }
                    else
                    {
                        if (mipData.m_Data.empty())
                        {
                            // not read yet, schedule for async IO
                            mipData.m_Data.resize(mipData.m_NumBytes);
                            AUTO_LOCK(m_MipIORequestsLock);
                            m_MipIORequests.push_back({ texUpdate.m_TextureIdx, tile });
                            //LOG_DEBUG("Scheduling mip IO for texture: %d, mip: %d", texUpdate.m_TextureIdx, tile.m_Mip);
                        }
                        else
                        {
                            // already in system memory. upload tile immediately to GPU
                            UploadTile(commandList, texUpdate.m_TextureIdx, tile);
                            //LOG_DEBUG("Uploading tile: texture: %d, mip: %d, tileIndex: %d", texUpdate.m_TextureIdx, tile.m_Mip, tileIndex);
                        }
                    }
                }
            }
        }
    }

    {
        PROFILE_SCOPED("Upload Deferred Tile Uploads");

        std::vector<MipIORequest> deferredTilesToUpload;
        {
            AUTO_LOCK(m_DeferredTilesToUploadLock);
            deferredTilesToUpload = std::move(m_DeferredTilesToUpload);
        }

        for (const MipIORequest& request : deferredTilesToUpload)
        {
            // Process each deferred tile upload request
            //LOG_DEBUG("Processing deferred tile upload: texture: %d, mip: %d", request.m_TextureIdx, request.m_TileInfo.m_Mip);
            UploadTile(commandList, request.m_TextureIdx, request.m_TileInfo);
        }
    }

    g_Graphic.EndCommandList(commandList, false);
    device->executeCommandList(commandList);
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

    const uint32_t startIdx = m_ResolveFeedbackTexturesCounter % g_Graphic.m_Textures.size();
    for (uint32_t i = 0; i < m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        if ((i > 0) && (i == startIdx))
        {
            break;
        }

        const uint32_t textureIdx = (m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
        Texture& texture = g_Graphic.m_Textures[textureIdx];

        if (texture.m_TiledTextureID != UINT_MAX)
        {
            commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
        }
    }

    m_ResolveFeedbackTexturesCounter = (m_ResolveFeedbackTexturesCounter + m_NumFeedbackTexturesToResolvePerFrame) % g_Graphic.m_Textures.size();
}

static const uint32_t kHeapSizeInTiles = 1024; // 64MiB heap size

uint32_t TextureFeedbackManager::AllocateHeap()
{
    PROFILE_FUNCTION();
    
    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    const uint32_t heapSizeInBytes = kHeapSizeInTiles * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes();

    nvrhi::HeapDesc heapDesc;
    heapDesc.capacity = heapSizeInBytes;
    heapDesc.type = nvrhi::HeapType::DeviceLocal;

    // TODO: Calling createHeap should ideally be called asynchronously to offload the critical path
    nvrhi::HeapHandle heap = device->createHeap(heapDesc);

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = heapSizeInBytes;
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

    m_HeapAllocationInBytes += heapSizeInBytes;
    ++m_NumHeaps;

    LOG_DEBUG("Allocated heap %u, total allocated: %.2f MB", heapID, BYTES_TO_MB(m_HeapAllocationInBytes));

    return heapID;
}

void TextureFeedbackManager::ReleaseHeap(uint32_t heapID)
{
    m_FreeHeapIDs.push_back(heapID);

    m_Heaps[heapID] = nullptr;
    m_Buffers[heapID] = nullptr;

    const uint32_t heapSizeInBytes = kHeapSizeInTiles * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes();

    m_HeapAllocationInBytes -= heapSizeInBytes;
    m_NumHeaps--;

    LOG_DEBUG("Released heap %u, total allocated: %.2f MB", heapID, BYTES_TO_MB(m_HeapAllocationInBytes));
}
