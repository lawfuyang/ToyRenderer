#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

struct DDSFileHeader
{
    uint32_t m_FileSize;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_MipCount;
    nvrhi::Format m_Format;
    uint32_t m_DXGIFormat; // DXGI_FORMAT enum value
    uint32_t m_ImageDataByteOffset;
};

bool IsDDSImage(FILE* file);
DDSFileHeader ReadDDSFileHeader(FILE* file);
void ReadDDSStreamingMipDatas(const DDSFileHeader& fileInfo, class Texture& texture);
void ReadDDSMipData(const DDSFileHeader& fileInfo, class Texture& texture, uint32_t mip, std::vector<std::byte>& data, uint32_t& memPitch);
