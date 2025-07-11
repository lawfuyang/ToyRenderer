#include "Scene.h"

#include "Engine.h"
#include "Graphic.h"

void Scene::AddTextureStreamingRequest(uint32_t textureIdx, int32_t targetMip)
{
    Texture& texture = m_Textures.at(textureIdx);

    if (texture.m_StreamingFilePath.empty())
    {
        return; // texture is not streamed
    }

    const uint32_t packedMipIdx = texture.m_PackedMipDesc.numStandardMips;

    targetMip = std::clamp(targetMip, 0, (int32_t)packedMipIdx);

    if (texture.m_InFlightStreamingMip == targetMip || texture.m_CurrentlyStreamedMip == targetMip)
    {
        return;
    }

    assert(texture.m_StreamingMipDatas[targetMip].IsValid());

    const bool bHigherDetailedMip = ((uint32_t)targetMip < texture.m_CurrentlyStreamedMip);
    if (bHigherDetailedMip)
    {
        if (texture.m_CurrentlyStreamedMip == 0)
        {
            return;
        }

        for (int32_t i = texture.m_CurrentlyStreamedMip - 1; i >= targetMip; --i)
        {
            AUTO_LOCK(m_TextureStreamingRequestsLock);
            m_TextureStreamingRequests.push_back(TextureStreamingRequest{ textureIdx, (uint32_t)i });
        }
    }
    else
    {
        if (texture.m_CurrentlyStreamedMip == packedMipIdx)
        {
            return;
        }

        for (int32_t i = texture.m_CurrentlyStreamedMip + 1; i <= targetMip; ++i)
        {
            AUTO_LOCK(m_TextureStreamingRequestsLock);
            m_TextureStreamingRequests.push_back(TextureStreamingRequest{ textureIdx, (uint32_t)i });
        }
    }

    texture.m_InFlightStreamingMip = (uint32_t)targetMip;
}

void Scene::ProcessTextureStreamingRequestsAsyncIO()
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
            TextureStreamingRequest* request = (TextureStreamingRequest*)asyncIOOutcome.userdata;
            assert(asyncIOOutcome.buffer == request->m_MipBytes.data());

            Texture& texture = m_Textures.at(request->m_TextureIdx);
            assert(texture.IsValid());

            assert(request->m_MipToStream != UINT_MAX);
            assert(request->m_MipToStream < std::size(texture.m_StreamingMipDatas));
            const StreamingMipData& streamingMipData = texture.m_StreamingMipDatas[request->m_MipToStream];
            assert(streamingMipData.IsValid());

            ON_EXIT_SCOPE_LAMBDA([request] { delete request; });

            assert(asyncIOOutcome.offset == streamingMipData.m_DataOffset);
            assert(asyncIOOutcome.bytes_requested == streamingMipData.m_NumBytes);
            assert(asyncIOOutcome.bytes_transferred == streamingMipData.m_NumBytes);
            assert(streamingMipData.m_NumBytes == request->m_MipBytes.size());
            
            // an IO operation should only be done if the requested mip is higher detailed than the currently streamed mip
            const bool bHigherDetailedMip = (request->m_MipToStream < texture.m_CurrentlyStreamedMip);
            assert(bHigherDetailedMip);

            {
                AUTO_LOCK(m_TextureStreamingRequestsToFinalizeLock);
                m_TextureStreamingRequestsToFinalize.push_back(std::move(*request));
            }

            // sanity check that the underlying bytes is empty after moving
            assert(request->m_MipBytes.empty());

            LOG_DEBUG("Texture Streaming Request Completed: Texture[%s] Mip[%u]", texture.m_NVRHITextureHandle->getDesc().debugName.c_str(), request->m_MipToStream);
        }
    };

    while (!m_bShutDownStreamingThread)
    {
        ProcessAsyncIOResults();
        
        std::vector<TextureStreamingRequest> textureStreamingRequests;
        {
            AUTO_LOCK(m_TextureStreamingRequestsLock);
            textureStreamingRequests = std::move(m_TextureStreamingRequests);
        }

        for (TextureStreamingRequest& request : textureStreamingRequests)
        {
            Texture& texture = m_Textures.at(request.m_TextureIdx);

            assert(texture.IsValid());
            assert(!texture.m_StreamingFilePath.empty());
            assert(request.m_MipToStream != UINT_MAX);
            assert(request.m_MipToStream < std::size(texture.m_StreamingMipDatas));

            const bool bHigherDetailedMip = (request.m_MipToStream < texture.m_CurrentlyStreamedMip);
            if (bHigherDetailedMip)
            {
                const StreamingMipData& streamingMipData = texture.m_StreamingMipDatas[request.m_MipToStream];
                assert(streamingMipData.IsValid());

                TextureStreamingRequest* inFlightRequest = new TextureStreamingRequest;
                inFlightRequest->m_TextureIdx = request.m_TextureIdx;
                inFlightRequest->m_MipToStream = request.m_MipToStream;
                inFlightRequest->m_MipBytes.resize(streamingMipData.m_NumBytes);

                SDL_AsyncIO* asyncIO = SDL_AsyncIOFromFile(texture.m_StreamingFilePath.c_str(), "r");
                SDL_CALL(asyncIO);

                SDL_CALL(SDL_ReadAsyncIO(asyncIO, inFlightRequest->m_MipBytes.data(), streamingMipData.m_DataOffset, streamingMipData.m_NumBytes, g_Engine.m_AsyncIOQueue, (void*)inFlightRequest));

                // according to the doc, we can close the async IO handle after the read request is submitted
                SDL_CALL(SDL_CloseAsyncIO(asyncIO, false, g_Engine.m_AsyncIOQueue, nullptr));

                // immediately process & discard the SDL_ASYNCIO_TASK_CLOSE result
                ProcessAsyncIOResults();
            }
            else
            {
                // immediately queue to finalize, because all we need to do is to evict the heap for the higher detailed mip(s)
                AUTO_LOCK(m_TextureStreamingRequestsToFinalizeLock);
                m_TextureStreamingRequestsToFinalize.push_back(std::move(request));
            }
        }
        textureStreamingRequests.clear();

        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // yield to avoid busy waiting
    }
}

void Scene::FinalizeTextureStreamingRequests()
{
    PROFILE_FUNCTION();

    std::vector<TextureStreamingRequest> textureStreamingRequestsToFinalize;
    {
        AUTO_LOCK(m_TextureStreamingRequestsToFinalizeLock);
        textureStreamingRequestsToFinalize = std::move(m_TextureStreamingRequestsToFinalize);
    }

    if (!textureStreamingRequestsToFinalize.empty())
    {
        PROFILE_SCOPED("Finalize Texture Streaming Requests");

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Finalize Texture Streaming Requests");

        for (TextureStreamingRequest& request : textureStreamingRequestsToFinalize)
        {
            PROFILE_SCOPED("Finalize Texture Streaming Request");

            Texture& texture = m_Textures.at(request.m_TextureIdx);

            const bool bHigherDetailedMip = (request.m_MipToStream < texture.m_CurrentlyStreamedMip);
            if (bHigherDetailedMip)
            {
                // TODO: update tile mapping to heap for mip
            }
            else
            {
                // TODO: update tile mapping to null heap to evict memory
            }

            texture.m_CurrentlyStreamedMip = request.m_MipToStream;

            LOG_DEBUG("Texture Streaming Request Finalized: Texture[%s] Mip[%u]", texture.m_NVRHITextureHandle->getDesc().debugName.c_str(), request.m_MipToStream);
        }

        // extern void TriggerDumpProfilingCapture(std::string_view fileName);
        // TriggerDumpProfilingCapture("TextureStreamingRequestsCapture");
    }
}

void Scene::StressTestTextureMipRequests()
{
    if (!m_bStressTestTextureMipRequests)
    {
        return;
    }

}
