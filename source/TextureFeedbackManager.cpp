#include "TextureFeedbackManager.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"

void TextureFeedbackManager::Initialize()
{
    m_TiledTextureManager = std::unique_ptr<rtxts::TiledTextureManager>{ rtxts::CreateTiledTextureManager(rtxts::TiledTextureManagerDesc{}) };
}

void TextureFeedbackManager::Shutdown()
{
    m_TiledTextureManager.reset();
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
