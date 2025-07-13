#include "TextureFeedbackManager.h"

void TextureFeedbackManager::Initialize()
{
    m_TiledTextureManager = std::unique_ptr<rtxts::TiledTextureManager>{ rtxts::CreateTiledTextureManager(rtxts::TiledTextureManagerDesc{}) };
}

void TextureFeedbackManager::Shutdown()
{
    m_TiledTextureManager.reset();
}
