#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

struct DDSFileInfo
{
    uint32_t m_FileSize;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_MipCount;
    nvrhi::Format m_Format;
    uint32_t m_DXGIFormat; // DXGI_FORMAT enum value
    uint32_t m_ImageDataByteOffset;
};

struct MipData
{
    uint32_t m_Mip;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_MemPitch;
    uint32_t m_MemSlicePitch;
    std::vector<std::byte> m_Data;
};

struct DDSReadParams
{
    FILE* m_File;
    class Texture* m_Texture;
    std::vector<MipData> m_MipDatas;
    uint32_t m_StartMipToRead;
    uint32_t m_NumMipsToRead;
};

bool IsDDSImage(FILE* file);
DDSFileInfo GetDDSFileInfo(FILE* file);
void ReadPackedDDSMipDatas(const DDSFileInfo& fileInfo, DDSReadParams& params);
