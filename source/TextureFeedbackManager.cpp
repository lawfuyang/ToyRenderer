#include "TextureFeedbackManager.h"

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

void TextureFeedbackManager::BeginFrame()
{
    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    if (!g_Graphic.m_Textures.empty())
    {
        for (uint32_t i = 0; i < g_Scene->m_NumFeedbackTexturesToResolvePerFrame; ++i)
        {
            const uint32_t textureIdx = (g_Scene->m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
            Texture& texture = g_Graphic.m_Textures[textureIdx];

            if (texture.m_SamplerFeedbackTextureHandle)
            {
                commandList->clearSamplerFeedbackTexture(texture.m_SamplerFeedbackTextureHandle);
            }
        }
    }
}

void TextureFeedbackManager::ResolveFeedback()
{
    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    for (uint32_t i = 0; i < g_Scene->m_NumFeedbackTexturesToResolvePerFrame; ++i)
    {
        const uint32_t textureIdx = (g_Scene->m_ResolveFeedbackTexturesCounter + i) % g_Graphic.m_Textures.size();
        Texture& texture = g_Graphic.m_Textures[textureIdx];

        if (texture.m_SamplerFeedbackTextureHandle)
        {
            commandList->decodeSamplerFeedbackTexture(texture.m_FeedbackResolveBuffers[g_Graphic.m_FrameCounter % 2], texture.m_SamplerFeedbackTextureHandle, nvrhi::Format::R8_UINT);
        }
    }
}
