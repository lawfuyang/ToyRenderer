#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

bool IsSTBImage(const void* data, uint32_t nbBytes);
bool IsDDSImage(const void* data);
bool IsSTBImage(FILE* file);
bool IsDDSImage(FILE* file);

nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB = false);
nvrhi::TextureHandle CreateDDSTextureFromFile(nvrhi::CommandListHandle commandList, FILE* file, uint32_t mipDataOffsets[13], const char* debugName);
