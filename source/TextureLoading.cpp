#include "extern/ktx_transcoder/basisu_transcoder.h"
#include "extern/stb/stb_image.h"

#include "Engine.h"
#include "Graphic.h"
#include "Utilities.h"

nvrhi::TextureHandle CreateKTXTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName)
{
    PROFILE_FUNCTION();

    basist::ktx2_transcoder transcoder;

    if (!transcoder.init(data, nbBytes))
    {
        assert(0);
    }

    // TODO: support cubemaps. For now we use KTX1 for cubemaps because basisu does not support HDR.
    if (transcoder.get_faces() == 6)
    {
        assert(0);
    }

    // TODO: support texture arrays.
    if (transcoder.get_layers() > 1)
    {
        assert(0);
    }

    const uint32_t nbMips = transcoder.get_levels();
    assert(nbMips < basist::KTX2_MAX_SUPPORTED_LEVEL_COUNT);

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = transcoder.get_dfd_transfer_func() == basist::KTX2_KHR_DF_TRANSFER_LINEAR ? nvrhi::Format::BC7_UNORM : nvrhi::Format::BC7_UNORM_SRGB;
    textureDesc.width = transcoder.get_width();
    textureDesc.height = transcoder.get_height();
    textureDesc.mipLevels = nbMips;
    textureDesc.debugName = debugName;
    textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;

    nvrhi::TextureHandle newTexture = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);

    for (uint32_t mip = 0; mip < nbMips; ++mip)
    {
        // just whack BC7 for everything according to: https://github.com/KhronosGroup/3D-Formats-Guidelines/blob/main/KTXDeveloperGuide.md
        const basist::transcoder_texture_format kRequestedFormat = basist::transcoder_texture_format::cTFBC7_RGBA;

        basist::ktx2_image_level_info levelInfo;
        transcoder.get_image_level_info(levelInfo, mip, 0 /*layerIndex*/, 0 /*faceIndex*/);

        const uint32_t qwordsPerBlock = basisu::get_qwords_per_block(basist::basis_get_basisu_texture_format(kRequestedFormat));
        const size_t byteCount = sizeof(uint64_t) * qwordsPerBlock * levelInfo.m_total_blocks;

        std::vector<std::byte> outputBlocksBytes;
        outputBlocksBytes.resize(byteCount);

        if (!transcoder.transcode_image_level(
            mip,
            0, // layerIndex,
            0, // faceIndex,
            outputBlocksBytes.data(),
            levelInfo.m_total_blocks,
            kRequestedFormat))
        {
            assert(0);
        }

        const uint32_t rowPitch = levelInfo.m_num_blocks_x * sizeof(uint64_t) * qwordsPerBlock;
        commandList->writeTexture(newTexture, 0, mip, outputBlocksBytes.data(), rowPitch);
    }

    commandList->setPermanentTextureState(newTexture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    return newTexture;
}

nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB = false)
{
    PROFILE_FUNCTION();

    int width = 0, height = 0, originalChannels = 0;

    if (!stbi_info_from_memory((const stbi_uc*)data, (int)nbBytes, &width, &height, &originalChannels))
    {
        LOG_DEBUG("STBI error: [%s]", stbi_failure_reason());
        assert(0);
    }

    const bool bIsHDR = stbi_is_hdr_from_memory((const stbi_uc*)data, (int)nbBytes);

    const int channels = originalChannels == 3 ? 4 : originalChannels;

    stbi_uc* bitmap;

    if (bIsHDR)
    {
        float* floatmap = stbi_loadf_from_memory((const stbi_uc*)data, (int)nbBytes, &width, &height, &originalChannels, channels);
        bitmap = reinterpret_cast<stbi_uc*>(floatmap);
    }
    else
    {
        bitmap = stbi_load_from_memory((const stbi_uc*)data, (int)nbBytes, &width, &height, &originalChannels, channels);
    }

    if (!bitmap)
    {
        LOG_DEBUG("STBI error: [%s]", stbi_failure_reason());
        assert(0);
    }

    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    switch (channels)
    {
    case 1:
        format = bIsHDR ? nvrhi::Format::R32_FLOAT : nvrhi::Format::R8_UNORM;
        break;
    case 2:
        format = bIsHDR ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RG8_UNORM;
        break;
    case 4:
        format = bIsHDR ? nvrhi::Format::RGBA32_FLOAT : (forceSRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM);
        break;
    default:
        LOG_DEBUG("Unsupported number of components (%d) for texture", channels);
        assert(0);
    }

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = format;
    textureDesc.width = static_cast<uint32_t>(width);
    textureDesc.height = static_cast<uint32_t>(height);
    textureDesc.depth = 1;
    textureDesc.arraySize = 1;
    textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
    textureDesc.mipLevels = 1;
    textureDesc.debugName = debugName;
    textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;

    nvrhi::TextureHandle newTexture = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);

    const uint32_t bytesPerPixel = channels * (bIsHDR ? 4 : 1);
    const uint32_t bytesPerPixelCheck = BytesPerPixel(format);
    assert(bytesPerPixel == bytesPerPixelCheck);

    commandList->writeTexture(newTexture, 0, 0, bitmap, static_cast<size_t>(width * bytesPerPixel), 0);
    commandList->setPermanentTextureState(newTexture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    stbi_image_free(bitmap);

    return newTexture;
}
