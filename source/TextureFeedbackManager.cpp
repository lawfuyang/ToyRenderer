#include "TextureFeedbackManager.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

static const uint32_t kHeapSizeInTiles = 1024; // 64MiB heap size

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

            // TODO
        }
    };

    while (!m_bShutDownAsyncIOThread)
    {
        ProcessAsyncIOResults();

        // TODO
    #if 0
        for (TextureStreamingRequest& request : textureStreamingRequests)
        {
            Texture& texture = g_Graphic.m_Textures.at(request.m_TextureIdx);

            assert(texture.IsValid());
            assert(!texture.m_StreamingFilePath.empty());

            SDL_AsyncIO* asyncIO = SDL_AsyncIOFromFile(texture.m_StreamingFilePath.c_str(), "r");
            SDL_CALL(asyncIO);


            SDL_CALL(SDL_ReadAsyncIO(asyncIO, inFlightRequest->m_MipBytes.data(), streamingMipData.m_DataOffset, streamingMipData.m_NumBytes, g_Engine.m_AsyncIOQueue, (void*)inFlightRequest));

            // according to the doc, we can close the async IO handle after the read request is submitted
            SDL_CALL(SDL_CloseAsyncIO(asyncIO, false, g_Engine.m_AsyncIOQueue, nullptr));

            // immediately process & discard the SDL_ASYNCIO_TASK_CLOSE result
            ProcessAsyncIOResults();

        }
    #endif

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
    rtxts::Statistics statistics = m_TiledTextureManager->GetStatistics();

    ImGui::Text("Heap allocation: %.2f MB", BYTES_TO_MB(m_HeapAllocationInBytes));
    ImGui::Text("Total tiles: %u", statistics.totalTilesNum);
    ImGui::Text("Allocated tiles: %u", statistics.allocatedTilesNum);
    ImGui::Text("Standby tiles: %u", statistics.standbyTilesNum);
    ImGui::Text("Free tiles in heaps: %u", statistics.heapFreeTilesNum);

    ImGui::SliderInt("Feedback Textures to Resolve Per Frame", &m_NumFeedbackTexturesToResolvePerFrame, 10, 100);
    ImGui::SliderFloat("Tile Timeout (seconds)", &m_TileTimeoutSeconds, 0.1f, 5.0f);
}

void TextureFeedbackManager::BeginFrame()
{
    if (g_Graphic.m_Textures.empty())
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    for (uint32_t textureIdx : m_TexturesToReadback)
    {
        Texture& texture = g_Graphic.m_Textures.at(textureIdx);
        nvrhi::BufferHandle resolveBuffer = texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2];
        void* pReadbackData = device->mapBuffer(resolveBuffer, nvrhi::CpuAccessMode::Read);

        rtxts::SamplerFeedbackDesc samplerFeedbackDesc = {};
        samplerFeedbackDesc.pMinMipData = (uint8_t*)pReadbackData;
        m_TiledTextureManager->UpdateWithSamplerFeedback(texture.m_TiledTextureID, samplerFeedbackDesc, g_Graphic.m_GraphicTimer.GetElapsedSeconds(), m_TileTimeoutSeconds);

        device->unmapBuffer(resolveBuffer);

        // TODO: call 'MatchPrimaryTexture' if necessary, whatever it means?
    }
    m_TexturesToReadback.clear();

    const uint32_t startIdx = m_ResolveFeedbackTexturesCounter % g_Graphic.m_Textures.size();
    for (uint32_t i = 0; i < m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        if ((i > 0) && (i == startIdx))
        {
            break;
        }

        const uint32_t textureIdx = (m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
        Texture& texture = g_Graphic.m_Textures[textureIdx];

        if (texture.m_SamplerFeedbackTextureHandle)
        {
            commandList->clearSamplerFeedbackTexture(texture.m_SamplerFeedbackTextureHandle);
            m_TexturesToReadback.push_back(textureIdx);
        }
    }

    {
        PROFILE_SCOPED("Trim Standby Tiles");
        m_TiledTextureManager->TrimStandbyTiles();
    }

    {
        PROFILE_SCOPED("Add/Release Heaps");

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
    std::unordered_set<nvrhi::TextureHandle> minMipDirtyTextures;
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
            const std::vector<rtxts::TileCoord>& tilesCoordinates = m_TiledTextureManager->GetTileCoordinates(texture.m_TiledTextureID);
            const uint32_t tileToUnmapNum = (uint32_t)tilesToUnmap.size();

            nvrhi::TiledTextureRegion tiledTextureRegion;
            tiledTextureRegion.tilesNum = 1;

            nvrhi::TextureTilesMapping textureTilesMapping;
            textureTilesMapping.numTextureRegions = tileToUnmapNum;
            std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates(textureTilesMapping.numTextureRegions);
            std::vector<nvrhi::TiledTextureRegion> tiledTextureRegions(textureTilesMapping.numTextureRegions, tiledTextureRegion);
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

            minMipDirtyTextures.insert(texture.m_MinMipTextureHandle);
        }

        if (!tilesToMap.empty())
        {
            FeedbackTextureUpdate& feedbackTextureUpdate = feedbackTextureUpdates.emplace_back();

            feedbackTextureUpdate.m_TextureIdx = i;
            for (uint32_t tileIndex : tilesToMap)
            {
                assert(std::find(feedbackTextureUpdate.m_TileIndices.begin(), feedbackTextureUpdate.m_TileIndices.end(), tileIndex) == feedbackTextureUpdate.m_TileIndices.end());
                feedbackTextureUpdate.m_TileIndices.push_back(tileIndex);
            }
        }
    }

    struct RequestedTile
    {
        uint32_t m_TextureIdx = UINT_MAX;
        uint32_t m_TileIndex = UINT_MAX;
    };
    std::vector<RequestedTile> requestedTiles;
    std::vector<RequestedTile> requestedPackedTiles;

    // Collect all tiles and store them in the queue
    for (FeedbackTextureUpdate& texUpdate : feedbackTextureUpdates)
    {
        RequestedTile reqTile;
        reqTile.m_TextureIdx = texUpdate.m_TextureIdx;
        for (uint32_t i = 0; i < texUpdate.m_TileIndices.size(); i++)
        {
            Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);
            reqTile.m_TileIndex = texUpdate.m_TileIndices[i];
            if (texture.IsTilePacked(reqTile.m_TileIndex))
                requestedPackedTiles.push_back(reqTile);
            else
                requestedTiles.push_back(reqTile);
        }
    }
    
    // Defragment up to 16 tiles per frame
    {
        PROFILE_SCOPED("Defragment Tiles");

        const uint32_t kNumTilesToDefragment = 16;
        m_TiledTextureManager->DefragmentTiles(kNumTilesToDefragment);
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

    // Upload only countUpload regular tiles
    // TODO: frame slice this
    for (const RequestedTile& regularTile : requestedTiles)
    {
        ScheduleTileForUpload(regularTile);
    }

    {
        PROFILE_SCOPED("Update Tile Mappings");

        for (FeedbackTextureUpdate& texUpdate : tilesThisFrame)
        {
            Texture& texture = g_Graphic.m_Textures.at(texUpdate.m_TextureIdx);

            minMipDirtyTextures.insert(texture.m_MinMipTextureHandle);

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

                    nvrhi::TiledTextureCoordinate tiledTextureCoordinate = {};
                    tiledTextureCoordinate.mipLevel = tilesCoordinates[tileIndex].mipLevel;
                    tiledTextureCoordinate.x = tilesCoordinates[tileIndex].x;
                    tiledTextureCoordinate.y = tilesCoordinates[tileIndex].y;
                    tiledTextureCoordinate.z = 0;
                    tiledTextureCoordinates.push_back(tiledTextureCoordinate);

                    nvrhi::TiledTextureRegion tiledTextureRegion = {};
                    tiledTextureRegion.tilesNum = 1;
                    tiledTextureRegions.push_back(tiledTextureRegion);

                    byteOffsets.push_back(tilesAllocations[tileIndex].heapTileIndex * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes());
                }

                nvrhi::TextureTilesMapping textureTilesMapping = {};
                textureTilesMapping.numTextureRegions = (uint32_t)tiledTextureCoordinates.size();
                textureTilesMapping.tiledTextureCoordinates = tiledTextureCoordinates.data();
                textureTilesMapping.tiledTextureRegions = tiledTextureRegions.data();
                textureTilesMapping.byteOffsets = byteOffsets.data();
                textureTilesMapping.heap = heap;

                device->updateTextureTileMappings(texture.m_NVRHITextureHandle, &textureTilesMapping, 1);
            }
        }
    }
}

void TextureFeedbackManager::EndFrame()
{
    if (g_Graphic.m_Textures.empty())
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

        if (texture.m_SamplerFeedbackTextureHandle)
        {
            commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
        }
    }

    m_ResolveFeedbackTexturesCounter = m_ResolveFeedbackTexturesCounter + (m_NumFeedbackTexturesToResolvePerFrame % g_Graphic.m_Textures.size());
}

uint32_t TextureFeedbackManager::AllocateHeap()
{
    PROFILE_FUNCTION();
    
    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    const uint32_t heapSizeInBytes = kHeapSizeInTiles * g_Graphic.m_GraphicRHI->GetTiledResourceSizeInBytes();

    nvrhi::HeapDesc heapDesc = {};
    heapDesc.capacity = heapSizeInBytes;
    heapDesc.type = nvrhi::HeapType::DeviceLocal;

    // TODO: Calling createHeap should ideally be called asynchronously to offload the critical path
    nvrhi::HeapHandle heap = device->createHeap(heapDesc);

    nvrhi::BufferDesc bufferDesc = {};
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
