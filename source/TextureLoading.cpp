#include "TextureLoading.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "Utilities.h"

static const char kDDSMagic[] = { 'D', 'D', 'S', ' ' };

static bool IsDDSImage(const void* data)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        if (((const char*)data)[i] != kDDSMagic[i])
        {
            return false;
        }
    }

    return true;
}

static nvrhi::Format ConvertFromDXGIFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return nvrhi::Format::RGBA8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return nvrhi::Format::SRGBA8_UNORM;

        // NOTE: we assume that if BC1_UNORM is requested, its for albedo texture, so we force it to SRGB
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return nvrhi::Format::BC1_UNORM_SRGB;

    case DXGI_FORMAT_BC2_UNORM:
        return nvrhi::Format::BC2_UNORM;
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return nvrhi::Format::BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM:
        return nvrhi::Format::BC3_UNORM;
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return nvrhi::Format::BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC4_UNORM:
        return nvrhi::Format::BC4_UNORM;
    case DXGI_FORMAT_BC4_SNORM:
        return nvrhi::Format::BC4_SNORM;
    case DXGI_FORMAT_BC5_UNORM:
        return nvrhi::Format::BC5_UNORM;
    case DXGI_FORMAT_BC5_SNORM:
        return nvrhi::Format::BC5_SNORM;
    case DXGI_FORMAT_BC7_UNORM:
        return nvrhi::Format::BC7_UNORM;
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return nvrhi::Format::BC7_UNORM_SRGB;
    default:
        assert(0);
    }

    return nvrhi::Format::UNKNOWN;
}

enum class PixelFormatFlagBits : uint32_t
{
    FourCC = 0x00000004,
    RGB = 0x00000040,
    RGBA = 0x00000041,
    Luminance = 0x00020000,
    LuminanceA = 0x00020001,
    AlphaPixels = 0x00000001,
    Alpha = 0x00000002,
    Palette8 = 0x00000020,
    Palette8A = 0x00000021,
    BumpDUDV = 0x00080000
};

enum class HeaderFlagBits : uint32_t
{
    Height = 0x00000002,
    Width = 0x00000004,
    Texture = 0x00001007,
    Mipmap = 0x00020000,
    Volume = 0x00800000,
    Pitch = 0x00000008,
    LinearSize = 0x00080000,
};

enum class HeaderCaps2FlagBits : uint32_t
{
    CubemapPositiveX = 0x00000600,
    CubemapNegativeX = 0x00000a00,
    CubemapPositiveY = 0x00001200,
    CubemapNegativeY = 0x00002200,
    CubemapPositiveZ = 0x00004200,
    CubemapNegativeZ = 0x00008200,
    CubemapAllFaces = CubemapPositiveX | CubemapNegativeX |
    CubemapPositiveY | CubemapNegativeY |
    CubemapPositiveZ | CubemapNegativeZ,
    Volume = 0x00200000,
};

struct PixelFormat
{
    uint32_t m_size;
    uint32_t m_flags;
    uint32_t m_fourCC;
    uint32_t m_bitCount;
    uint32_t m_RBitMask;
    uint32_t m_GBitMask;
    uint32_t m_BBitMask;
    uint32_t m_ABitMask;
};

struct Header
{
    uint32_t m_size;
    uint32_t m_flags;
    uint32_t m_height;
    uint32_t m_width;
    uint32_t m_pitchOrLinerSize;
    uint32_t m_depth;
    uint32_t m_mipMapCount;
    uint32_t m_reserved1[11];
    PixelFormat m_pixelFormat;
    uint32_t m_caps;
    uint32_t m_caps2;
    uint32_t m_caps3;
    uint32_t m_caps4;
    uint32_t m_reserved2;
};

enum class TextureDimension : uint32_t
{
    Unknown = 0,
    Texture1D = 2,
    Texture2D = 3,
    Texture3D = 4
};

enum class DXT10MiscFlagBits : uint32_t { TextureCube = 0x4 };

struct HeaderDXT10
{
    DXGI_FORMAT dxgiFormat;
    TextureDimension m_resourceDimension;
    uint32_t m_miscFlag;
    uint32_t m_arraySize;
    uint32_t m_miscFlag2;
};

constexpr static uint32_t MakeFourCC(char ch0, char ch1, char ch2, char ch3)
{
    return (uint32_t(uint8_t(ch0)) | (uint32_t(uint8_t(ch1)) << 8) | (uint32_t(uint8_t(ch2)) << 16) | (uint32_t(uint8_t(ch3)) << 24));
}

static DXGI_FORMAT GetDXGIFormat(const PixelFormat& pf)
{
    if (pf.m_flags & uint32_t(PixelFormatFlagBits::RGB))
    {
        switch (pf.m_bitCount)
        {
        case 32:
            if (pf.m_RBitMask == 0x000000ff &&
                pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x00ff0000 &&
                pf.m_ABitMask == 0xff000000)
            {
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            }
            if (pf.m_RBitMask == 0x00ff0000 &&
                pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x000000ff &&
                pf.m_ABitMask == 0xff000000)
            {
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
            if (pf.m_RBitMask == 0x00ff0000 &&
                pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x000000ff &&
                pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_B8G8R8X8_UNORM;
            }

            if (pf.m_RBitMask == 0x0000ffff &&
                pf.m_GBitMask == 0xffff0000 &&
                pf.m_BBitMask == 0x00000000 &&
                pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R16G16_UNORM;
            }

            if (pf.m_RBitMask == 0xffffffff &&
                pf.m_GBitMask == 0x00000000 &&
                pf.m_BBitMask == 0x00000000 &&
                pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R32_FLOAT;
            }
            break;
        case 24:
            break;
        case 16:
            if (pf.m_RBitMask == 0x7c00 && pf.m_GBitMask == 0x03e0 &&
                pf.m_BBitMask == 0x001f && pf.m_ABitMask == 0x8000)
            {
                return DXGI_FORMAT_B5G5R5A1_UNORM;
            }
            if (pf.m_RBitMask == 0xf800 && pf.m_GBitMask == 0x07e0 &&
                pf.m_BBitMask == 0x001f && pf.m_ABitMask == 0x0000)
            {
                return DXGI_FORMAT_B5G6R5_UNORM;
            }

            if (pf.m_RBitMask == 0x0f00 && pf.m_GBitMask == 0x00f0 &&
                pf.m_BBitMask == 0x000f && pf.m_ABitMask == 0xf000)
            {
                return DXGI_FORMAT_B4G4R4A4_UNORM;
            }
            break;
        default:
            break;
        }
    }
    else if (pf.m_flags & uint32_t(PixelFormatFlagBits::Luminance))
    {
        if (8 == pf.m_bitCount)
        {
            if (pf.m_RBitMask == 0x000000ff && pf.m_GBitMask == 0x00000000 &&
                pf.m_BBitMask == 0x00000000 && pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R8_UNORM;
            }
            if (pf.m_RBitMask == 0x000000ff && pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x00000000 && pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R8G8_UNORM;
            }
        }
        if (16 == pf.m_bitCount)
        {
            if (pf.m_RBitMask == 0x0000ffff && pf.m_GBitMask == 0x00000000 &&
                pf.m_BBitMask == 0x00000000 && pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R16_UNORM;
            }
            if (pf.m_RBitMask == 0x000000ff && pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x00000000 && pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R8G8_UNORM;
            }
        }
    }
    else if (pf.m_flags & uint32_t(PixelFormatFlagBits::Alpha))
    {
        if (8 == pf.m_bitCount)
        {
            return DXGI_FORMAT_A8_UNORM;
        }
    }
    else if (pf.m_flags & uint32_t(PixelFormatFlagBits::BumpDUDV))
    {
        if (16 == pf.m_bitCount)
        {
            if (pf.m_RBitMask == 0x00ff && pf.m_GBitMask == 0xff00 &&
                pf.m_BBitMask == 0x0000 && pf.m_ABitMask == 0x0000)
            {
                return DXGI_FORMAT_R8G8_SNORM;
            }
        }
        if (32 == pf.m_bitCount)
        {
            if (pf.m_RBitMask == 0x000000ff && pf.m_GBitMask == 0x0000ff00 &&
                pf.m_BBitMask == 0x00ff0000 && pf.m_ABitMask == 0xff000000)
            {
                return DXGI_FORMAT_R8G8B8A8_SNORM;
            }
            if (pf.m_RBitMask == 0x0000ffff && pf.m_GBitMask == 0xffff0000 &&
                pf.m_BBitMask == 0x00000000 && pf.m_ABitMask == 0x00000000)
            {
                return DXGI_FORMAT_R16G16_SNORM;
            }
        }
    }
    else if (pf.m_flags & uint32_t(PixelFormatFlagBits::FourCC))
    {
        if (MakeFourCC('D', 'X', 'T', '1') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC1_UNORM;
        }
        if (MakeFourCC('D', 'X', 'T', '3') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC2_UNORM;
        }
        if (MakeFourCC('D', 'X', 'T', '5') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC3_UNORM;
        }

        if (MakeFourCC('D', 'X', 'T', '4') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC2_UNORM;
        }
        if (MakeFourCC('D', 'X', 'T', '5') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC3_UNORM;
        }

        if (MakeFourCC('A', 'T', 'I', '1') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC4_UNORM;
        }
        if (MakeFourCC('B', 'C', '4', 'U') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC4_UNORM;
        }
        if (MakeFourCC('B', 'C', '4', 'S') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC4_SNORM;
        }

        if (MakeFourCC('A', 'T', 'I', '2') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC5_UNORM;
        }
        if (MakeFourCC('B', 'C', '5', 'U') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC5_UNORM;
        }
        if (MakeFourCC('B', 'C', '5', 'S') == pf.m_fourCC)
        {
            return DXGI_FORMAT_BC5_SNORM;
        }

        if (MakeFourCC('R', 'G', 'B', 'G') == pf.m_fourCC)
        {
            return DXGI_FORMAT_R8G8_B8G8_UNORM;
        }
        if (MakeFourCC('G', 'R', 'G', 'B') == pf.m_fourCC)
        {
            return DXGI_FORMAT_G8R8_G8B8_UNORM;
        }

        if (MakeFourCC('Y', 'U', 'Y', '2') == pf.m_fourCC)
        {
            return DXGI_FORMAT_YUY2;
        }

        switch (pf.m_fourCC)
        {
        case 36:
            return DXGI_FORMAT_R16G16B16A16_UNORM;
        case 110:
            return DXGI_FORMAT_R16G16B16A16_SNORM;
        case 111:
            return DXGI_FORMAT_R16_FLOAT;
        case 112:
            return DXGI_FORMAT_R16G16_FLOAT;
        case 113:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 114:
            return DXGI_FORMAT_R32_FLOAT;
        case 115:
            return DXGI_FORMAT_R32G32_FLOAT;
        case 116:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }

    return DXGI_FORMAT_UNKNOWN;
}

static uint32_t GetBitsPerPixel(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 96;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 64;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
        return 32;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        return 24;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return 16;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return 12;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
        return 8;

    case DXGI_FORMAT_R1_UNORM:
        return 1;

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 4;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8;
    default:
        return 0;
    }
}

static void GetImageInfo(uint32_t w, uint32_t h, DXGI_FORMAT fmt, uint32_t& outNumBytes, uint32_t& outRowBytes, uint32_t& outNumRows)
{
    uint32_t numBytes = 0;
    uint32_t rowBytes = 0;
    uint32_t numRows = 0;

    bool bc = false;
    bool packed = false;
    bool planar = false;
    uint32_t bpe = 0;
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        bc = true;
        bpe = 8;
        break;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        bc = true;
        bpe = 16;
        break;

    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_YUY2:
        packed = true;
        bpe = 4;
        break;

    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        packed = true;
        bpe = 8;
        break;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
        planar = true;
        bpe = 2;
        break;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        planar = true;
        bpe = 4;
        break;
    default:
        break;
    }

    if (bc)
    {
        uint32_t numBlocksWide = 0;
        if (w > 0)
        {
            numBlocksWide = std::max<uint32_t>(1, (w + 3) / 4);
        }
        uint32_t numBlocksHigh = 0;
        if (h > 0)
        {
            numBlocksHigh = std::max<uint32_t>(1, (h + 3) / 4);
        }
        rowBytes = numBlocksWide * bpe;
        numRows = numBlocksHigh;
        numBytes = rowBytes * numBlocksHigh;
    }
    else if (packed)
    {
        rowBytes = ((w + 1) >> 1) * bpe;
        numRows = h;
        numBytes = rowBytes * h;
    }
    else if (fmt == DXGI_FORMAT_NV11)
    {
        rowBytes = ((w + 3) >> 2) * 4;
        numRows = h * 2;
        numBytes = rowBytes + numRows;
    }
    else if (planar)
    {
        rowBytes = ((w + 1) >> 1) * bpe;
        numBytes = (rowBytes * h) + ((rowBytes * h + 1) >> 1);
        numRows = h + ((h + 1) >> 1);
    }
    else
    {
        uint32_t bpp = GetBitsPerPixel(fmt);
        rowBytes = (w * bpp + 7) / 8;
        numRows = h;
        numBytes = rowBytes * h;
    }

    outNumBytes = numBytes;
    outRowBytes = rowBytes;
    outNumRows = numRows;
}

void ReadDDSTextureFileHeader(FILE* f, Texture& texture)
{
    assert(f);

    fseek(f, 0, SEEK_END);

    const uint32_t fileSize = ftell(f);
    assert(fileSize >= 4);

    fseek(f, 0, SEEK_SET);

    uint32_t fileReadOffset = 0;

    char magic[4];
    if (fread(magic, sizeof(char), sizeof(kDDSMagic), f) != std::size(kDDSMagic))
    {
        assert(0);
    }
    assert(IsDDSImage(magic));
    fileReadOffset += sizeof(kDDSMagic);

    Header header;
    if (fread(&header, sizeof(header), 1, f) != 1)
    {
        assert(0);
    }
    fileReadOffset += sizeof(Header);

    if (header.m_size != sizeof(Header) || header.m_pixelFormat.m_size != sizeof(PixelFormat))
    {
        assert(0);
    }

    const uint32_t kDX10FourCC = MakeFourCC('D', 'X', '1', '0');

    bool bIsDXT10Header = false;
    if ((header.m_pixelFormat.m_flags & uint32_t(PixelFormatFlagBits::FourCC)) && (header.m_pixelFormat.m_fourCC == kDX10FourCC))
    {
        if ((sizeof(uint32_t) + sizeof(Header) + sizeof(HeaderDXT10)) >= fileSize)
        {
            assert(0);
        }
        bIsDXT10Header = true;
    }

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    assert(header.m_mipMapCount != 0);

    if (bIsDXT10Header)
    {
        HeaderDXT10 dxt10Header;
        if (fread(&dxt10Header, sizeof(dxt10Header), 1, f) != 1)
        {
            assert(0);
        }
        fileReadOffset += sizeof(HeaderDXT10);

        assert(dxt10Header.m_arraySize == 1);

        switch (dxt10Header.dxgiFormat)
        {
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
        case DXGI_FORMAT_A8P8:
            assert(0);
        default:
            if (GetBitsPerPixel(dxt10Header.dxgiFormat) == 0)
            {
                assert(0);
            }
        }

        // dont support 3d & cubemap textures
        assert(dxt10Header.m_resourceDimension == TextureDimension::Texture2D);
        assert(!(dxt10Header.m_miscFlag & uint32_t(DXT10MiscFlagBits::TextureCube)));

        dxgiFormat = dxt10Header.dxgiFormat;
    }
    else
    {
        dxgiFormat = GetDXGIFormat(header.m_pixelFormat);
        assert(dxgiFormat != DXGI_FORMAT_UNKNOWN);

        // dont support 3d & cubemap textures
        assert(!(header.m_flags & uint32_t(HeaderFlagBits::Volume)));
        assert(!(header.m_caps2 & uint32_t(HeaderCaps2FlagBits::CubemapAllFaces)));
    }

    TextureFileHeader& outHeader = texture.m_TextureFileHeader;
    outHeader.m_FileSize = fileSize;
    outHeader.m_Width = header.m_width;
    outHeader.m_Height = header.m_height;
    outHeader.m_MipCount = header.m_mipMapCount;
    outHeader.m_Format = ConvertFromDXGIFormat(dxgiFormat);
    outHeader.m_DXGIFormat = dxgiFormat;
    outHeader.m_ImageDataByteOffset = fileReadOffset;
}

void ReadDDSMipInfos(Texture& texture)
{
    PROFILE_FUNCTION();

    const TextureFileHeader& fileInfo = texture.m_TextureFileHeader;
    uint32_t fileReadOffset = fileInfo.m_ImageDataByteOffset;

    for (uint32_t i = 0; i < fileInfo.m_MipCount; ++i)
    {
        const uint32_t mipWidth = std::max<uint32_t>(1, fileInfo.m_Width >> i);
        const uint32_t mipHeight = std::max<uint32_t>(1, fileInfo.m_Height >> i);

        uint32_t numBytes;
        uint32_t rowBytes;
        uint32_t numRows;
        GetImageInfo(mipWidth, mipHeight, (DXGI_FORMAT)fileInfo.m_DXGIFormat, numBytes, rowBytes, numRows);

        TextureMipData& TextureMipData = texture.m_TextureMipDatas[i];
        TextureMipData.m_Resolution = { mipWidth, mipHeight };
        TextureMipData.m_DataOffset = fileReadOffset;
        TextureMipData.m_NumBytes = numBytes;
        TextureMipData.m_RowPitch = rowBytes;

        fileReadOffset += numBytes;
        assert(fileReadOffset <= fileInfo.m_FileSize);
    }

    assert(fileReadOffset == fileInfo.m_FileSize);
}

void ReadDDSMipData(Texture& texture, FILE* f, uint32_t mip)
{
    PROFILE_FUNCTION();

    const TextureFileHeader& fileInfo = texture.m_TextureFileHeader;
    TextureMipData& TextureMipData = texture.m_TextureMipDatas[mip];
    assert(TextureMipData.IsValid());

    assert(TextureMipData.m_Data.empty());
    TextureMipData.m_Data.resize(TextureMipData.m_NumBytes);

    assert(f);
    fseek(f, TextureMipData.m_DataOffset, SEEK_SET);

    const uint32_t bytesRead = fread(TextureMipData.m_Data.data(), sizeof(std::byte), TextureMipData.m_NumBytes, f);
    assert(bytesRead == TextureMipData.m_NumBytes);
}
