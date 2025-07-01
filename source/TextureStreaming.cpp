#include "Scene.h"

#include "Engine.h"
#include "Graphic.h"

void Scene::AddTextureStreamingRequest(Texture& texture, bool bHigherDetailedMip)
{
    if (texture.m_StreamingFilePath.empty())
    {
        return; // texture is not streamed
    }

    if (texture.m_HighestStreamedMip == 0 && bHigherDetailedMip)
    {
        return; // no mip to lower
    }

    if (!bHigherDetailedMip)
    {
        const Vector2U currentMipRes = texture.m_StreamingMipDatas[texture.m_HighestStreamedMip].m_Resolution;
        const uint32_t currentMipMaxSize = std::max(currentMipRes.x, currentMipRes.y);
        const bool bIsPackedMip = currentMipMaxSize <= Graphic::kPackedMipResolution;
        if (bIsPackedMip)
        {
            return; // no mip to raise
        }
    }

    const uint32_t requestedMip = ((int)texture.m_HighestStreamedMip + (bHigherDetailedMip ? -1 : 1));
    assert(texture.m_StreamingMipDatas[requestedMip].IsValid());

    if (texture.m_InFlightStreamingMip == requestedMip)
    {
        return; // already in flight
    }

    texture.m_InFlightStreamingMip = requestedMip;

    AUTO_LOCK(m_TextureStreamingRequestsLock);
    m_TextureStreamingRequests.push_back(TextureStreamingRequest{ &texture, requestedMip });
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
            const uint32_t inFlightIdx = ((uintptr_t)asyncIOOutcome.userdata) & 0x7FFFFFFF; // clear the highest bit to get the original index
            TextureStreamingRequest& request = m_InFlightTextureStreamingRequests.at(inFlightIdx);
            assert(asyncIOOutcome.buffer == request.m_MipBytes.data());

            assert(request.m_Texture);
            Texture& texture = *request.m_Texture;
            assert(texture.IsValid());

            assert(request.m_RequestedMip != UINT_MAX);
            assert(request.m_RequestedMip < std::size(texture.m_StreamingMipDatas));
            const StreamingMipData& streamingMipData = texture.m_StreamingMipDatas[request.m_RequestedMip];
            assert(streamingMipData.IsValid());

            assert(asyncIOOutcome.offset == streamingMipData.m_DataOffset);
            assert(asyncIOOutcome.bytes_requested == streamingMipData.m_NumBytes);
            assert(asyncIOOutcome.bytes_transferred == streamingMipData.m_NumBytes);
            assert(streamingMipData.m_NumBytes == request.m_MipBytes.size());
            
            // an IO operation should only be done if the requested mip is higher detailed than the currently streamed mip
            const bool bHigherDetailedMip = (request.m_RequestedMip < texture.m_HighestStreamedMip);
            assert(bHigherDetailedMip);

            const nvrhi::TextureDesc& originalDesc = texture.m_NVRHITextureHandle->getDesc();

            nvrhi::TextureDesc newDesc = originalDesc;
            newDesc.width = streamingMipData.m_Resolution.x;
            newDesc.height = streamingMipData.m_Resolution.y;
            newDesc.mipLevels = originalDesc.mipLevels + 1;

            request.m_NewTextureHandle = g_Graphic.m_NVRHIDevice->createTexture(newDesc);

            {
                AUTO_LOCK(m_TextureStreamingRequestsToFinalizeLock);
                m_TextureStreamingRequestsToFinalize.push_back(std::move(request));
            }

            // LOG_DEBUG("Texture Streaming Request Completed: Texture[%s] Mip[%u] Frame[%u]", texture.m_NVRHITextureHandle->getDesc().debugName.c_str(), request.m_RequestedMip, g_Graphic.m_FrameCounter);
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
            assert(request.m_Texture);
            Texture& texture = *request.m_Texture;

            assert(texture.IsValid());
            assert(!texture.m_StreamingFilePath.empty());
            assert(request.m_RequestedMip != UINT_MAX);
            assert(request.m_RequestedMip < std::size(texture.m_StreamingMipDatas));

            // check that requested mip is only '1' diff from the highest streamed mip
            assert(std::abs((int)texture.m_HighestStreamedMip - (int)request.m_RequestedMip) == 1);

            const StreamingMipData& streamingMipData = texture.m_StreamingMipDatas[request.m_RequestedMip];
            assert(streamingMipData.IsValid());

            const bool bHigherDetailedMip = (request.m_RequestedMip < texture.m_HighestStreamedMip);

            if (bHigherDetailedMip)
            {
                request.m_MipBytes.resize(streamingMipData.m_NumBytes);

                uint32_t inFlightidx = UINT_MAX;
                for (uint32_t i = 0; i < m_InFlightTextureStreamingRequests.size(); ++i)
                {
                    if (m_InFlightTextureStreamingRequests[i].m_MipBytes.empty())
                    {
                        inFlightidx = i;
                        break;
                    }
                }
                if (inFlightidx == UINT_MAX)
                {
                    // no in-flight request slot available, just push a new one
                    inFlightidx = m_InFlightTextureStreamingRequests.size();
                    m_InFlightTextureStreamingRequests.push_back(TextureStreamingRequest{});
                }

                SDL_AsyncIO* asyncIO = SDL_AsyncIOFromFile(texture.m_StreamingFilePath.c_str(), "r");
                SDL_CALL(asyncIO);

                TextureStreamingRequest& inFlightRequest = m_InFlightTextureStreamingRequests[inFlightidx];
                inFlightRequest.m_Texture = request.m_Texture;
                inFlightRequest.m_RequestedMip = request.m_RequestedMip;
                inFlightRequest.m_MipBytes.resize(streamingMipData.m_NumBytes);

                inFlightidx |= 0x80000000; // use the highest bit to mark as in-flight, mainly so that i can simply check for nullptr for userdata ptr
                SDL_CALL(SDL_ReadAsyncIO(asyncIO, inFlightRequest.m_MipBytes.data(), streamingMipData.m_DataOffset, streamingMipData.m_NumBytes, g_Engine.m_AsyncIOQueue, (void*)(uintptr_t)inFlightidx));

                // according to the doc, we can close the async IO handle after the read request is submitted
                SDL_CALL(SDL_CloseAsyncIO(asyncIO, false, g_Engine.m_AsyncIOQueue, nullptr));

                ProcessAsyncIOResults();

                m_InFlightTextureStreamingRequests.push_back(std::move(request));
            }
            else
            {
                const nvrhi::TextureDesc& originalDesc = texture.m_NVRHITextureHandle->getDesc();

                nvrhi::TextureDesc newDesc = originalDesc;
                newDesc.width = streamingMipData.m_Resolution.x;
                newDesc.height = streamingMipData.m_Resolution.y;
                newDesc.mipLevels = originalDesc.mipLevels - 1;

                request.m_NewTextureHandle = g_Graphic.m_NVRHIDevice->createTexture(newDesc);

                {
                    AUTO_LOCK(m_TextureStreamingRequestsToFinalizeLock);
                    m_TextureStreamingRequestsToFinalize.push_back(std::move(request));
                }
            }
        }

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

            assert(request.m_NewTextureHandle);

            assert(request.m_Texture);
            Texture& texture = *request.m_Texture;
            const StreamingMipData& streamingMipData = texture.m_StreamingMipDatas[request.m_RequestedMip];

            const bool bHigherDetailedMip = (request.m_RequestedMip < texture.m_HighestStreamedMip);

            const nvrhi::TextureDesc& originalDesc = texture.m_NVRHITextureHandle->getDesc();

            if (bHigherDetailedMip)
            {
                // copy newly streamed in highest mip data
                commandList->writeTexture(request.m_NewTextureHandle, 0, 0, request.m_MipBytes.data(), streamingMipData.m_RowPitch);
            }

            // copy rest of mips from original texture
            const uint32_t numMipsToCopy = bHigherDetailedMip ? originalDesc.mipLevels : (originalDesc.mipLevels - 1);
            for (uint32_t i = 0; i < numMipsToCopy; ++i)
            {
                nvrhi::TextureSlice srcSlice;
                srcSlice.mipLevel = bHigherDetailedMip ? i : (i + 1);

                nvrhi::TextureSlice destSlice;
                destSlice.mipLevel = bHigherDetailedMip ? (i + 1) : i;

                commandList->copyTexture(request.m_NewTextureHandle, destSlice, texture.m_NVRHITextureHandle, srcSlice);
            }

            nvrhi::TextureHandle originalTextureHandle = texture.m_NVRHITextureHandle;

            texture.m_NVRHITextureHandle = request.m_NewTextureHandle;
            texture.m_HighestStreamedMip = request.m_RequestedMip;

            // update texture descriptor index
            DescriptorTableManager* descriptorTableManager = g_Graphic.m_SrvUavCbvDescriptorTableManager.get();
            descriptorTableManager->ReleaseDescriptor(originalTextureHandle->srvIndexInTable);

            // sanity check: new texture handle not in the descriptor table yet
            assert(texture.m_NVRHITextureHandle->srvIndexInTable == UINT_MAX);

            const uint32_t newTableIndex = descriptorTableManager->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, texture.m_NVRHITextureHandle));

            // sanity check: make sure that the descriptor index is the exact same
            assert(originalTextureHandle->srvIndexInTable == newTableIndex);

            texture.m_NVRHITextureHandle->srvIndexInTable = newTableIndex;

            //LOG_DEBUG("Texture Streaming Request Finalized: Texture[%s] Mip[%u]", texture.m_NVRHITextureHandle->getDesc().debugName.c_str(), request.m_RequestedMip);
        }

        // extern void TriggerDumpProfilingCapture(std::string_view fileName);
        // TriggerDumpProfilingCapture("TextureStreamingRequestsCapture");
    }
}
