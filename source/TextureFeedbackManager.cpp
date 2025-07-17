#include "TextureFeedbackManager.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

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
    ImGui::Text("Heap allocation: %.2f MB", BYTES_TO_MB(m_HeapAllocationInBytes));
    ImGui::Text("Total tiles: %u", m_Statistics.totalTilesNum);
    ImGui::Text("Allocated tiles: %u", m_Statistics.allocatedTilesNum);
    ImGui::Text("Standby tiles: %u", m_Statistics.standbyTilesNum);
    ImGui::Text("Free tiles in heaps: %u", m_Statistics.heapFreeTilesNum);

    ImGui::SliderFloat("Tile Timeout (seconds)", &m_TileTimeoutSeconds, 0.1f, 5.0f);
}

void TextureFeedbackManager::BeginFrame()
{
    if (g_Graphic.m_Textures.empty())
    {
        return;
    }

    PROFILE_FUNCTION();

    m_Statistics = m_TiledTextureManager->GetStatistics();

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
    }
    m_TexturesToReadback.clear();

    const uint32_t startIdx = g_Scene->m_ResolveFeedbackTexturesCounter % g_Graphic.m_Textures.size();
    for (uint32_t i = 0; i < g_Scene->m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        if ((i > 0) && (i == startIdx))
        {
            break;
        }

        const uint32_t textureIdx = (g_Scene->m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
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

    const uint32_t numRequiredHeaps = m_TiledTextureManager->GetNumDesiredHeaps();
}

void TextureFeedbackManager::ResolveFeedback()
{
    if (g_Graphic.m_Textures.empty())
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    const uint32_t startIdx = g_Scene->m_ResolveFeedbackTexturesCounter % g_Graphic.m_Textures.size();
    for (uint32_t i = 0; i < g_Scene->m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        if ((i > 0) && (i == startIdx))
        {
            break;
        }

        const uint32_t textureIdx = (g_Scene->m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
        Texture& texture = g_Graphic.m_Textures[textureIdx];

        if (texture.m_SamplerFeedbackTextureHandle)
        {
            commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
        }
    }
}

void TextureFeedbackManager::AllocateHeap(uint32_t& heapId)
{

}
