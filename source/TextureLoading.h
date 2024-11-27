#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

bool IsDDSImage(const void* data);
nvrhi::TextureHandle CreateDDSTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName);

bool IsSTBImage(const void* data, uint32_t nbBytes);
nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB = false);

bool IsKTX2Image(const void* data, uint32_t nbBytes);
nvrhi::TextureHandle CreateKTXTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName);
