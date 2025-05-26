#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "extern/stb/stb_image.h"

#include "Engine.h"
#include "Graphic.h"
#include "Utilities.h"

bool IsDDSImage(const void* data)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        static const char kMagic[] = { 'D', 'D', 'S', ' ' };

        if (((const char*)data)[i] != kMagic[i])
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

// modified from https://github.com/benikabocha/tinyddsloader
struct DDSFile
{
    enum class Result
    {
        Success,
        ErrorFileOpen,
        ErrorRead,
        ErrorMagicWord,
        ErrorSize,
        ErrorVerify,
        ErrorNotSupported,
        ErrorInvalidData,
    };

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
        DXGI_FORMAT m_format;
        TextureDimension m_resourceDimension;
        uint32_t m_miscFlag;
        uint32_t m_arraySize;
        uint32_t m_miscFlag2;
    };

    struct ImageData
    {
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_depth;
        const void* m_mem;
        uint32_t m_memPitch;
        uint32_t m_memSlicePitch;
    };

    struct BC1Block
    {
        uint16_t m_color0;
        uint16_t m_color1;
        uint8_t m_row0;
        uint8_t m_row1;
        uint8_t m_row2;
        uint8_t m_row3;
    };

    struct BC2Block
    {
        uint16_t m_alphaRow0;
        uint16_t m_alphaRow1;
        uint16_t m_alphaRow2;
        uint16_t m_alphaRow3;
        uint16_t m_color0;
        uint16_t m_color1;
        uint8_t m_row0;
        uint8_t m_row1;
        uint8_t m_row2;
        uint8_t m_row3;
    };

    struct BC3Block
    {
        uint8_t m_alpha0;
        uint8_t m_alpha1;
        uint8_t m_alphaR0;
        uint8_t m_alphaR1;
        uint8_t m_alphaR2;
        uint8_t m_alphaR3;
        uint8_t m_alphaR4;
        uint8_t m_alphaR5;
        uint16_t m_color0;
        uint16_t m_color1;
        uint8_t m_row0;
        uint8_t m_row1;
        uint8_t m_row2;
        uint8_t m_row3;
    };

    struct BC4Block
    {
        uint8_t m_red0;
        uint8_t m_red1;
        uint8_t m_redR0;
        uint8_t m_redR1;
        uint8_t m_redR2;
        uint8_t m_redR3;
        uint8_t m_redR4;
        uint8_t m_redR5;
    };

    struct BC5Block
    {
        uint8_t m_red0;
        uint8_t m_red1;
        uint8_t m_redR0;
        uint8_t m_redR1;
        uint8_t m_redR2;
        uint8_t m_redR3;
        uint8_t m_redR4;
        uint8_t m_redR5;
        uint8_t m_green0;
        uint8_t m_green1;
        uint8_t m_greenR0;
        uint8_t m_greenR1;
        uint8_t m_greenR2;
        uint8_t m_greenR3;
        uint8_t m_greenR4;
        uint8_t m_greenR5;
    };

    bool IsCompressed(DXGI_FORMAT fmt)
    {
        switch (fmt)
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        }
        return false;
    }

    uint32_t MakeFourCC(char ch0, char ch1, char ch2, char ch3)
    {
        return (uint32_t(uint8_t(ch0)) | (uint32_t(uint8_t(ch1)) << 8) |
                (uint32_t(uint8_t(ch2)) << 16) | (uint32_t(uint8_t(ch3)) << 24));
    }

    DXGI_FORMAT GetDXGIFormat(const PixelFormat& pf)
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

    uint32_t GetBitsPerPixel(DXGI_FORMAT fmt)
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

    Result Load(const char* filepath)
    {
        std::ifstream input(filepath, std::ios_base::binary);
        if (!input.is_open())
        {
            return Result::ErrorFileOpen;
        }

        input.seekg(0, std::ios_base::beg);
        auto begPos = input.tellg();
        input.seekg(0, std::ios_base::end);
        auto endPos = input.tellg();
        input.seekg(0, std::ios_base::beg);

        auto fileSize = endPos - begPos;
        if (fileSize == 0)
        {
            return Result::ErrorRead;
        }
        std::vector<uint8_t> dds(fileSize);

        input.read(reinterpret_cast<char*>(dds.data()), fileSize);

        if (input.bad())
        {
            return Result::ErrorRead;
        }

        return Load(dds);
    }

    Result Load(std::span<const uint8_t> dds)
    {
        if (dds.size() < 4)
        {
            return Result::ErrorSize;
        }

        if (!IsDDSImage(dds.data()))
        {
            return Result::ErrorMagicWord;
        }

        if ((sizeof(uint32_t) + sizeof(Header)) >= dds.size())
        {
            return Result::ErrorSize;
        }

        const Header* header = reinterpret_cast<const Header*>(dds.data() + sizeof(uint32_t));

        if (header->m_size != sizeof(Header) ||
            header->m_pixelFormat.m_size != sizeof(PixelFormat))
        {
            return Result::ErrorVerify;
        }

        bool bIsDXT10Header = false;
        if ((header->m_pixelFormat.m_flags &
             uint32_t(PixelFormatFlagBits::FourCC)) &&
            (MakeFourCC('D', 'X', '1', '0') == header->m_pixelFormat.m_fourCC))
        {
            if ((sizeof(uint32_t) + sizeof(Header) + sizeof(HeaderDXT10)) >=
                dds.size())
            {
                return Result::ErrorSize;
            }
            bIsDXT10Header = true;
        }

        ptrdiff_t offset = sizeof(uint32_t) + sizeof(Header) +
            (bIsDXT10Header ? sizeof(HeaderDXT10) : 0);

        m_height = header->m_height;
        m_width = header->m_width;
        m_texDim = TextureDimension::Unknown;
        m_arraySize = 1;
        m_format = DXGI_FORMAT_UNKNOWN;
        m_isCubemap = false;
        m_mipCount = header->m_mipMapCount;
        if (0 == m_mipCount)
        {
            m_mipCount = 1;
        }

        if (bIsDXT10Header)
        {
            const HeaderDXT10* dxt10Header = reinterpret_cast<const HeaderDXT10*>(
                reinterpret_cast<const char*>(header) + sizeof(Header));

            m_arraySize = dxt10Header->m_arraySize;
            if (m_arraySize == 0)
            {
                return Result::ErrorInvalidData;
            }

            switch (dxt10Header->m_format)
            {
            case DXGI_FORMAT_AI44:
            case DXGI_FORMAT_IA44:
            case DXGI_FORMAT_P8:
            case DXGI_FORMAT_A8P8:
                return Result::ErrorNotSupported;
            default:
                if (GetBitsPerPixel(dxt10Header->m_format) == 0)
                {
                    return Result::ErrorNotSupported;
                }
            }

            m_format = dxt10Header->m_format;

            switch (dxt10Header->m_resourceDimension)
            {
            case TextureDimension::Texture1D:
                if ((header->m_flags & uint32_t(HeaderFlagBits::Height) &&
                     (m_height != 1)))
                {
                    return Result::ErrorInvalidData;
                }
                m_height = m_depth = 1;
                break;
            case TextureDimension::Texture2D:
                if (dxt10Header->m_miscFlag &
                    uint32_t(DXT10MiscFlagBits::TextureCube))
                {
                    m_arraySize *= 6;
                    m_isCubemap = true;
                }
                m_depth = 1;
                break;
            case TextureDimension::Texture3D:
                if (!(header->m_flags & uint32_t(HeaderFlagBits::Volume)))
                {
                    return Result::ErrorInvalidData;
                }
                if (m_arraySize > 1)
                {
                    return Result::ErrorNotSupported;
                }
                break;
            default:
                return Result::ErrorNotSupported;
            }

            m_texDim = dxt10Header->m_resourceDimension;
        }
        else
        {
            m_format = GetDXGIFormat(header->m_pixelFormat);
            if (m_format == DXGI_FORMAT_UNKNOWN)
            {
                return Result::ErrorNotSupported;
            }

            if (header->m_flags & uint32_t(HeaderFlagBits::Volume))
            {
                m_texDim = TextureDimension::Texture3D;
            }
            else
            {
                uint32_t caps2 = header->m_caps2 &
                    uint32_t(HeaderCaps2FlagBits::CubemapAllFaces);
                if (caps2)
                {
                    if (caps2 != uint32_t(HeaderCaps2FlagBits::CubemapAllFaces))
                    {
                        return Result::ErrorNotSupported;
                    }
                    m_arraySize = 6;
                    m_isCubemap = true;
                }

                m_depth = 1;
                m_texDim = TextureDimension::Texture2D;
            }
        }

        std::vector<ImageData> imageDatas(m_mipCount * m_arraySize);
        const uint8_t* srcBits = dds.data() + offset;
        const uint8_t* endBits = dds.data() + dds.size();
        uint32_t idx = 0;
        for (uint32_t j = 0; j < m_arraySize; j++)
        {
            uint32_t w = m_width;
            uint32_t h = m_height;
            uint32_t d = m_depth;
            for (uint32_t i = 0; i < m_mipCount; i++)
            {
                uint32_t numBytes;
                uint32_t rowBytes;
                GetImageInfo(w, h, m_format, &numBytes, &rowBytes, nullptr);

                imageDatas[idx].m_width = w;
                imageDatas[idx].m_height = h;
                imageDatas[idx].m_depth = d;
                imageDatas[idx].m_mem = srcBits;
                imageDatas[idx].m_memPitch = rowBytes;
                imageDatas[idx].m_memSlicePitch = numBytes;
                idx++;

                if (srcBits + (numBytes * d) > endBits)
                {
                    return Result::ErrorInvalidData;
                }
                srcBits += numBytes * d;
                w = std::max<uint32_t>(1, w / 2);
                h = std::max<uint32_t>(1, h / 2);
                d = std::max<uint32_t>(1, d / 2);
            }
        }

        m_imageDatas = std::move(imageDatas);

        m_NVRHIFormat = ConvertFromDXGIFormat(m_format);

        return Result::Success;
    }

    const ImageData* GetImageData(uint32_t mipIdx = 0, uint32_t arrayIdx = 0) const
    {
        if (mipIdx < m_mipCount && arrayIdx < m_arraySize)
        {
            return &m_imageDatas[m_mipCount * arrayIdx + mipIdx];
        }
        return nullptr;
    }

    void GetImageInfo(uint32_t w, uint32_t h, DXGI_FORMAT fmt,
                      uint32_t* outNumBytes, uint32_t* outRowBytes,
                      uint32_t* outNumRows)
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

        if (outNumBytes)
        {
            *outNumBytes = numBytes;
        }
        if (outRowBytes)
        {
            *outRowBytes = rowBytes;
        }
        if (outNumRows)
        {
            *outNumRows = numRows;
        }
    }

    bool Flip()
    {
        if (IsCompressed(m_format))
        {
            for (auto& imageData : m_imageDatas)
            {
                if (!FlipCompressedImage(imageData))
                {
                    return false;
                }
            }
        }
        else
        {
            for (auto& imageData : m_imageDatas)
            {
                if (!FlipImage(imageData))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool FlipImage(ImageData& imageData)
    {
        for (uint32_t y = 0; y < imageData.m_height / 2; y++)
        {
            auto line0 = (uint8_t*)imageData.m_mem + y * imageData.m_memPitch;
            auto line1 = (uint8_t*)imageData.m_mem +
                (imageData.m_height - y - 1) * imageData.m_memPitch;
            for (uint32_t i = 0; i < imageData.m_memPitch; i++)
            {
                std::swap(*line0, *line1);
                line0++;
                line1++;
            }
        }
        return true;
    }

    bool FlipCompressedImage(ImageData& imageData)
    {
        if (DXGI_FORMAT_BC1_TYPELESS == m_format ||
            DXGI_FORMAT_BC1_UNORM == m_format ||
            DXGI_FORMAT_BC1_UNORM_SRGB == m_format)
        {
            FlipCompressedImageBC1(imageData);
            return true;
        }
        else if (DXGI_FORMAT_BC2_TYPELESS == m_format ||
                 DXGI_FORMAT_BC2_UNORM == m_format ||
                 DXGI_FORMAT_BC2_UNORM_SRGB == m_format)
        {
            FlipCompressedImageBC2(imageData);
            return true;
        }
        else if (DXGI_FORMAT_BC3_TYPELESS == m_format ||
                 DXGI_FORMAT_BC3_UNORM == m_format ||
                 DXGI_FORMAT_BC3_UNORM_SRGB == m_format)
        {
            FlipCompressedImageBC3(imageData);
            return true;
        }
        else if (DXGI_FORMAT_BC4_TYPELESS == m_format ||
                 DXGI_FORMAT_BC4_UNORM == m_format ||
                 DXGI_FORMAT_BC4_SNORM == m_format)
        {
            FlipCompressedImageBC4(imageData);
            return true;
        }
        else if (DXGI_FORMAT_BC5_TYPELESS == m_format ||
                 DXGI_FORMAT_BC5_UNORM == m_format ||
                 DXGI_FORMAT_BC5_SNORM == m_format)
        {
            FlipCompressedImageBC5(imageData);
            return true;
        }
        return false;
    }

    void FlipCompressedImageBC1(ImageData& imageData)
    {
        uint32_t numXBlocks = (imageData.m_width + 3) / 4;
        uint32_t numYBlocks = (imageData.m_height + 3) / 4;
        if (imageData.m_height == 1)
        {
        }
        else if (imageData.m_height == 2)
        {
            auto blocks = (BC1Block*)imageData.m_mem;
            for (uint32_t x = 0; x < numXBlocks; x++)
            {
                auto block = blocks + x;
                std::swap(block->m_row0, block->m_row1);
                std::swap(block->m_row2, block->m_row3);
            }
        }
        else
        {
            for (uint32_t y = 0; y < (numYBlocks + 1) / 2; y++)
            {
                auto blocks0 = (BC1Block*)((uint8_t*)imageData.m_mem +
                                           imageData.m_memPitch * y);
                auto blocks1 =
                    (BC1Block*)((uint8_t*)imageData.m_mem +
                                imageData.m_memPitch * (numYBlocks - y - 1));
                for (uint32_t x = 0; x < numXBlocks; x++)
                {
                    auto block0 = blocks0 + x;
                    auto block1 = blocks1 + x;
                    if (blocks0 != blocks1)
                    {
                        std::swap(block0->m_color0, block1->m_color0);
                        std::swap(block0->m_color1, block1->m_color1);
                        std::swap(block0->m_row0, block1->m_row3);
                        std::swap(block0->m_row1, block1->m_row2);
                        std::swap(block0->m_row2, block1->m_row1);
                        std::swap(block0->m_row3, block1->m_row0);
                    }
                    else
                    {
                        std::swap(block0->m_row0, block0->m_row3);
                        std::swap(block0->m_row1, block0->m_row2);
                    }
                }
            }
        }
    }

    void FlipCompressedImageBC2(ImageData& imageData)
    {
        uint32_t numXBlocks = (imageData.m_width + 3) / 4;
        uint32_t numYBlocks = (imageData.m_height + 3) / 4;
        if (imageData.m_height == 1)
        {
        }
        else if (imageData.m_height == 2)
        {
            auto blocks = (BC2Block*)imageData.m_mem;
            for (uint32_t x = 0; x < numXBlocks; x++)
            {
                auto block = blocks + x;
                std::swap(block->m_alphaRow0, block->m_alphaRow1);
                std::swap(block->m_alphaRow2, block->m_alphaRow3);
                std::swap(block->m_row0, block->m_row1);
                std::swap(block->m_row2, block->m_row3);
            }
        }
        else
        {
            for (uint32_t y = 0; y < (numYBlocks + 1) / 2; y++)
            {
                auto blocks0 = (BC2Block*)((uint8_t*)imageData.m_mem +
                                           imageData.m_memPitch * y);
                auto blocks1 =
                    (BC2Block*)((uint8_t*)imageData.m_mem +
                                imageData.m_memPitch * (numYBlocks - y - 1));
                for (uint32_t x = 0; x < numXBlocks; x++)
                {
                    auto block0 = blocks0 + x;
                    auto block1 = blocks1 + x;
                    if (block0 != block1)
                    {
                        std::swap(block0->m_alphaRow0, block1->m_alphaRow3);
                        std::swap(block0->m_alphaRow1, block1->m_alphaRow2);
                        std::swap(block0->m_alphaRow2, block1->m_alphaRow1);
                        std::swap(block0->m_alphaRow3, block1->m_alphaRow0);
                        std::swap(block0->m_color0, block1->m_color0);
                        std::swap(block0->m_color1, block1->m_color1);
                        std::swap(block0->m_row0, block1->m_row3);
                        std::swap(block0->m_row1, block1->m_row2);
                        std::swap(block0->m_row2, block1->m_row1);
                        std::swap(block0->m_row3, block1->m_row0);
                    }
                    else
                    {
                        std::swap(block0->m_alphaRow0, block0->m_alphaRow3);
                        std::swap(block0->m_alphaRow1, block0->m_alphaRow2);
                        std::swap(block0->m_row0, block0->m_row3);
                        std::swap(block0->m_row1, block0->m_row2);
                    }
                }
            }
        }
    }

    void FlipCompressedImageBC3(ImageData& imageData)
    {
        uint32_t numXBlocks = (imageData.m_width + 3) / 4;
        uint32_t numYBlocks = (imageData.m_height + 3) / 4;
        if (imageData.m_height == 1)
        {
        }
        else if (imageData.m_height == 2)
        {
            auto blocks = (BC3Block*)imageData.m_mem;
            for (uint32_t x = 0; x < numXBlocks; x++)
            {
                auto block = blocks + x;
                uint8_t r0 = (block->m_alphaR1 >> 4) | (block->m_alphaR2 << 4);
                uint8_t r1 = (block->m_alphaR2 >> 4) | (block->m_alphaR0 << 4);
                uint8_t r2 = (block->m_alphaR0 >> 4) | (block->m_alphaR1 << 4);
                uint8_t r3 = (block->m_alphaR4 >> 4) | (block->m_alphaR5 << 4);
                uint8_t r4 = (block->m_alphaR5 >> 4) | (block->m_alphaR3 << 4);
                uint8_t r5 = (block->m_alphaR3 >> 4) | (block->m_alphaR4 << 4);

                block->m_alphaR0 = r0;
                block->m_alphaR1 = r1;
                block->m_alphaR2 = r2;
                block->m_alphaR3 = r3;
                block->m_alphaR4 = r4;
                block->m_alphaR5 = r5;
                std::swap(block->m_row0, block->m_row1);
                std::swap(block->m_row2, block->m_row3);
            }
        }
        else
        {
            for (uint32_t y = 0; y < (numYBlocks + 1) / 2; y++)
            {
                auto blocks0 = (BC3Block*)((uint8_t*)imageData.m_mem +
                                           imageData.m_memPitch * y);
                auto blocks1 =
                    (BC3Block*)((uint8_t*)imageData.m_mem +
                                imageData.m_memPitch * (numYBlocks - y - 1));
                for (uint32_t x = 0; x < numXBlocks; x++)
                {
                    auto block0 = blocks0 + x;
                    auto block1 = blocks1 + x;
                    if (block0 != block1)
                    {
                        std::swap(block0->m_alpha0, block1->m_alpha0);
                        std::swap(block0->m_alpha1, block1->m_alpha1);

                        uint8_t r0[6];
                        r0[0] = (block0->m_alphaR4 >> 4) | (block0->m_alphaR5 << 4);
                        r0[1] = (block0->m_alphaR5 >> 4) | (block0->m_alphaR3 << 4);
                        r0[2] = (block0->m_alphaR3 >> 4) | (block0->m_alphaR4 << 4);
                        r0[3] = (block0->m_alphaR1 >> 4) | (block0->m_alphaR2 << 4);
                        r0[4] = (block0->m_alphaR2 >> 4) | (block0->m_alphaR0 << 4);
                        r0[5] = (block0->m_alphaR0 >> 4) | (block0->m_alphaR1 << 4);
                        uint8_t r1[6];
                        r1[0] = (block1->m_alphaR4 >> 4) | (block1->m_alphaR5 << 4);
                        r1[1] = (block1->m_alphaR5 >> 4) | (block1->m_alphaR3 << 4);
                        r1[2] = (block1->m_alphaR3 >> 4) | (block1->m_alphaR4 << 4);
                        r1[3] = (block1->m_alphaR1 >> 4) | (block1->m_alphaR2 << 4);
                        r1[4] = (block1->m_alphaR2 >> 4) | (block1->m_alphaR0 << 4);
                        r1[5] = (block1->m_alphaR0 >> 4) | (block1->m_alphaR1 << 4);

                        block0->m_alphaR0 = r1[0];
                        block0->m_alphaR1 = r1[1];
                        block0->m_alphaR2 = r1[2];
                        block0->m_alphaR3 = r1[3];
                        block0->m_alphaR4 = r1[4];
                        block0->m_alphaR5 = r1[5];

                        block1->m_alphaR0 = r0[0];
                        block1->m_alphaR1 = r0[1];
                        block1->m_alphaR2 = r0[2];
                        block1->m_alphaR3 = r0[3];
                        block1->m_alphaR4 = r0[4];
                        block1->m_alphaR5 = r0[5];

                        std::swap(block0->m_color0, block1->m_color0);
                        std::swap(block0->m_color1, block1->m_color1);
                        std::swap(block0->m_row0, block1->m_row3);
                        std::swap(block0->m_row1, block1->m_row2);
                        std::swap(block0->m_row2, block1->m_row1);
                        std::swap(block0->m_row3, block1->m_row0);
                    }
                    else
                    {
                        uint8_t r0[6];
                        r0[0] = (block0->m_alphaR4 >> 4) | (block0->m_alphaR5 << 4);
                        r0[1] = (block0->m_alphaR5 >> 4) | (block0->m_alphaR3 << 4);
                        r0[2] = (block0->m_alphaR3 >> 4) | (block0->m_alphaR4 << 4);
                        r0[3] = (block0->m_alphaR1 >> 4) | (block0->m_alphaR2 << 4);
                        r0[4] = (block0->m_alphaR2 >> 4) | (block0->m_alphaR0 << 4);
                        r0[5] = (block0->m_alphaR0 >> 4) | (block0->m_alphaR1 << 4);

                        block0->m_alphaR0 = r0[0];
                        block0->m_alphaR1 = r0[1];
                        block0->m_alphaR2 = r0[2];
                        block0->m_alphaR3 = r0[3];
                        block0->m_alphaR4 = r0[4];
                        block0->m_alphaR5 = r0[5];

                        std::swap(block0->m_row0, block0->m_row3);
                        std::swap(block0->m_row1, block0->m_row2);
                    }
                }
            }
        }
    }

    void FlipCompressedImageBC4(ImageData& imageData)
    {
        uint32_t numXBlocks = (imageData.m_width + 3) / 4;
        uint32_t numYBlocks = (imageData.m_height + 3) / 4;
        if (imageData.m_height == 1)
        {
        }
        else if (imageData.m_height == 2)
        {
            auto blocks = (BC4Block*)imageData.m_mem;
            for (uint32_t x = 0; x < numXBlocks; x++)
            {
                auto block = blocks + x;
                uint8_t r0 = (block->m_redR1 >> 4) | (block->m_redR2 << 4);
                uint8_t r1 = (block->m_redR2 >> 4) | (block->m_redR0 << 4);
                uint8_t r2 = (block->m_redR0 >> 4) | (block->m_redR1 << 4);
                uint8_t r3 = (block->m_redR4 >> 4) | (block->m_redR5 << 4);
                uint8_t r4 = (block->m_redR5 >> 4) | (block->m_redR3 << 4);
                uint8_t r5 = (block->m_redR3 >> 4) | (block->m_redR4 << 4);

                block->m_redR0 = r0;
                block->m_redR1 = r1;
                block->m_redR2 = r2;
                block->m_redR3 = r3;
                block->m_redR4 = r4;
                block->m_redR5 = r5;
            }
        }
        else
        {
            for (uint32_t y = 0; y < (numYBlocks + 1) / 2; y++)
            {
                auto blocks0 = (BC4Block*)((uint8_t*)imageData.m_mem +
                                           imageData.m_memPitch * y);
                auto blocks1 =
                    (BC4Block*)((uint8_t*)imageData.m_mem +
                                imageData.m_memPitch * (numYBlocks - y - 1));
                for (uint32_t x = 0; x < numXBlocks; x++)
                {
                    auto block0 = blocks0 + x;
                    auto block1 = blocks1 + x;
                    if (block0 != block1)
                    {
                        std::swap(block0->m_red0, block1->m_red0);
                        std::swap(block0->m_red1, block1->m_red1);

                        uint8_t r0[6];
                        r0[0] = (block0->m_redR4 >> 4) | (block0->m_redR5 << 4);
                        r0[1] = (block0->m_redR5 >> 4) | (block0->m_redR3 << 4);
                        r0[2] = (block0->m_redR3 >> 4) | (block0->m_redR4 << 4);
                        r0[3] = (block0->m_redR1 >> 4) | (block0->m_redR2 << 4);
                        r0[4] = (block0->m_redR2 >> 4) | (block0->m_redR0 << 4);
                        r0[5] = (block0->m_redR0 >> 4) | (block0->m_redR1 << 4);
                        uint8_t r1[6];
                        r1[0] = (block1->m_redR4 >> 4) | (block1->m_redR5 << 4);
                        r1[1] = (block1->m_redR5 >> 4) | (block1->m_redR3 << 4);
                        r1[2] = (block1->m_redR3 >> 4) | (block1->m_redR4 << 4);
                        r1[3] = (block1->m_redR1 >> 4) | (block1->m_redR2 << 4);
                        r1[4] = (block1->m_redR2 >> 4) | (block1->m_redR0 << 4);
                        r1[5] = (block1->m_redR0 >> 4) | (block1->m_redR1 << 4);

                        block0->m_redR0 = r1[0];
                        block0->m_redR1 = r1[1];
                        block0->m_redR2 = r1[2];
                        block0->m_redR3 = r1[3];
                        block0->m_redR4 = r1[4];
                        block0->m_redR5 = r1[5];

                        block1->m_redR0 = r0[0];
                        block1->m_redR1 = r0[1];
                        block1->m_redR2 = r0[2];
                        block1->m_redR3 = r0[3];
                        block1->m_redR4 = r0[4];
                        block1->m_redR5 = r0[5];

                    }
                    else
                    {
                        uint8_t r0[6];
                        r0[0] = (block0->m_redR4 >> 4) | (block0->m_redR5 << 4);
                        r0[1] = (block0->m_redR5 >> 4) | (block0->m_redR3 << 4);
                        r0[2] = (block0->m_redR3 >> 4) | (block0->m_redR4 << 4);
                        r0[3] = (block0->m_redR1 >> 4) | (block0->m_redR2 << 4);
                        r0[4] = (block0->m_redR2 >> 4) | (block0->m_redR0 << 4);
                        r0[5] = (block0->m_redR0 >> 4) | (block0->m_redR1 << 4);

                        block0->m_redR0 = r0[0];
                        block0->m_redR1 = r0[1];
                        block0->m_redR2 = r0[2];
                        block0->m_redR3 = r0[3];
                        block0->m_redR4 = r0[4];
                        block0->m_redR5 = r0[5];
                    }
                }
            }
        }
    }

    void FlipCompressedImageBC5(ImageData& imageData)
    {
        uint32_t numXBlocks = (imageData.m_width + 3) / 4;
        uint32_t numYBlocks = (imageData.m_height + 3) / 4;
        if (imageData.m_height == 1)
        {
        }
        else if (imageData.m_height == 2)
        {
            auto blocks = (BC5Block*)imageData.m_mem;
            for (uint32_t x = 0; x < numXBlocks; x++)
            {
                auto block = blocks + x;
                uint8_t r0 = (block->m_redR1 >> 4) | (block->m_redR2 << 4);
                uint8_t r1 = (block->m_redR2 >> 4) | (block->m_redR0 << 4);
                uint8_t r2 = (block->m_redR0 >> 4) | (block->m_redR1 << 4);
                uint8_t r3 = (block->m_redR4 >> 4) | (block->m_redR5 << 4);
                uint8_t r4 = (block->m_redR5 >> 4) | (block->m_redR3 << 4);
                uint8_t r5 = (block->m_redR3 >> 4) | (block->m_redR4 << 4);

                block->m_redR0 = r0;
                block->m_redR1 = r1;
                block->m_redR2 = r2;
                block->m_redR3 = r3;
                block->m_redR4 = r4;
                block->m_redR5 = r5;

                uint8_t g0 = (block->m_greenR1 >> 4) | (block->m_greenR2 << 4);
                uint8_t g1 = (block->m_greenR2 >> 4) | (block->m_greenR0 << 4);
                uint8_t g2 = (block->m_greenR0 >> 4) | (block->m_greenR1 << 4);
                uint8_t g3 = (block->m_greenR4 >> 4) | (block->m_greenR5 << 4);
                uint8_t g4 = (block->m_greenR5 >> 4) | (block->m_greenR3 << 4);
                uint8_t g5 = (block->m_greenR3 >> 4) | (block->m_greenR4 << 4);

                block->m_greenR0 = g0;
                block->m_greenR1 = g1;
                block->m_greenR2 = g2;
                block->m_greenR3 = g3;
                block->m_greenR4 = g4;
                block->m_greenR5 = g5;
            }
        }
        else
        {
            for (uint32_t y = 0; y < (numYBlocks + 1) / 2; y++)
            {
                auto blocks0 = (BC5Block*)((uint8_t*)imageData.m_mem +
                                           imageData.m_memPitch * y);
                auto blocks1 =
                    (BC5Block*)((uint8_t*)imageData.m_mem +
                                imageData.m_memPitch * (numYBlocks - y - 1));
                for (uint32_t x = 0; x < numXBlocks; x++)
                {
                    auto block0 = blocks0 + x;
                    auto block1 = blocks1 + x;
                    if (block0 != block1)
                    {
                        std::swap(block0->m_red0, block1->m_red0);
                        std::swap(block0->m_red1, block1->m_red1);

                        uint8_t r0[6];
                        r0[0] = (block0->m_redR4 >> 4) | (block0->m_redR5 << 4);
                        r0[1] = (block0->m_redR5 >> 4) | (block0->m_redR3 << 4);
                        r0[2] = (block0->m_redR3 >> 4) | (block0->m_redR4 << 4);
                        r0[3] = (block0->m_redR1 >> 4) | (block0->m_redR2 << 4);
                        r0[4] = (block0->m_redR2 >> 4) | (block0->m_redR0 << 4);
                        r0[5] = (block0->m_redR0 >> 4) | (block0->m_redR1 << 4);
                        uint8_t r1[6];
                        r1[0] = (block1->m_redR4 >> 4) | (block1->m_redR5 << 4);
                        r1[1] = (block1->m_redR5 >> 4) | (block1->m_redR3 << 4);
                        r1[2] = (block1->m_redR3 >> 4) | (block1->m_redR4 << 4);
                        r1[3] = (block1->m_redR1 >> 4) | (block1->m_redR2 << 4);
                        r1[4] = (block1->m_redR2 >> 4) | (block1->m_redR0 << 4);
                        r1[5] = (block1->m_redR0 >> 4) | (block1->m_redR1 << 4);

                        block0->m_redR0 = r1[0];
                        block0->m_redR1 = r1[1];
                        block0->m_redR2 = r1[2];
                        block0->m_redR3 = r1[3];
                        block0->m_redR4 = r1[4];
                        block0->m_redR5 = r1[5];

                        block1->m_redR0 = r0[0];
                        block1->m_redR1 = r0[1];
                        block1->m_redR2 = r0[2];
                        block1->m_redR3 = r0[3];
                        block1->m_redR4 = r0[4];
                        block1->m_redR5 = r0[5];

                        std::swap(block0->m_green0, block1->m_green0);
                        std::swap(block0->m_green1, block1->m_green1);

                        uint8_t g0[6];
                        g0[0] = (block0->m_greenR4 >> 4) | (block0->m_greenR5 << 4);
                        g0[1] = (block0->m_greenR5 >> 4) | (block0->m_greenR3 << 4);
                        g0[2] = (block0->m_greenR3 >> 4) | (block0->m_greenR4 << 4);
                        g0[3] = (block0->m_greenR1 >> 4) | (block0->m_greenR2 << 4);
                        g0[4] = (block0->m_greenR2 >> 4) | (block0->m_greenR0 << 4);
                        g0[5] = (block0->m_greenR0 >> 4) | (block0->m_greenR1 << 4);
                        uint8_t g1[6];
                        g1[0] = (block1->m_greenR4 >> 4) | (block1->m_greenR5 << 4);
                        g1[1] = (block1->m_greenR5 >> 4) | (block1->m_greenR3 << 4);
                        g1[2] = (block1->m_greenR3 >> 4) | (block1->m_greenR4 << 4);
                        g1[3] = (block1->m_greenR1 >> 4) | (block1->m_greenR2 << 4);
                        g1[4] = (block1->m_greenR2 >> 4) | (block1->m_greenR0 << 4);
                        g1[5] = (block1->m_greenR0 >> 4) | (block1->m_greenR1 << 4);

                        block0->m_greenR0 = g1[0];
                        block0->m_greenR1 = g1[1];
                        block0->m_greenR2 = g1[2];
                        block0->m_greenR3 = g1[3];
                        block0->m_greenR4 = g1[4];
                        block0->m_greenR5 = g1[5];

                        block1->m_greenR0 = g0[0];
                        block1->m_greenR1 = g0[1];
                        block1->m_greenR2 = g0[2];
                        block1->m_greenR3 = g0[3];
                        block1->m_greenR4 = g0[4];
                        block1->m_greenR5 = g0[5];
                    }
                    else
                    {
                        uint8_t r0[6];
                        r0[0] = (block0->m_redR4 >> 4) | (block0->m_redR5 << 4);
                        r0[1] = (block0->m_redR5 >> 4) | (block0->m_redR3 << 4);
                        r0[2] = (block0->m_redR3 >> 4) | (block0->m_redR4 << 4);
                        r0[3] = (block0->m_redR1 >> 4) | (block0->m_redR2 << 4);
                        r0[4] = (block0->m_redR2 >> 4) | (block0->m_redR0 << 4);
                        r0[5] = (block0->m_redR0 >> 4) | (block0->m_redR1 << 4);

                        block0->m_redR0 = r0[0];
                        block0->m_redR1 = r0[1];
                        block0->m_redR2 = r0[2];
                        block0->m_redR3 = r0[3];
                        block0->m_redR4 = r0[4];
                        block0->m_redR5 = r0[5];

                        uint8_t g0[6];
                        g0[0] = (block0->m_greenR4 >> 4) | (block0->m_greenR5 << 4);
                        g0[1] = (block0->m_greenR5 >> 4) | (block0->m_greenR3 << 4);
                        g0[2] = (block0->m_greenR3 >> 4) | (block0->m_greenR4 << 4);
                        g0[3] = (block0->m_greenR1 >> 4) | (block0->m_greenR2 << 4);
                        g0[4] = (block0->m_greenR2 >> 4) | (block0->m_greenR0 << 4);
                        g0[5] = (block0->m_greenR0 >> 4) | (block0->m_greenR1 << 4);

                        block0->m_greenR0 = g0[0];
                        block0->m_greenR1 = g0[1];
                        block0->m_greenR2 = g0[2];
                        block0->m_greenR3 = g0[3];
                        block0->m_greenR4 = g0[4];
                        block0->m_greenR5 = g0[5];
                    }
                }
            }
        }
    }

    std::vector<ImageData> m_imageDatas;

    uint32_t m_height;
    uint32_t m_width;
    uint32_t m_depth;
    uint32_t m_mipCount;
    uint32_t m_arraySize;
    DXGI_FORMAT m_format;
    nvrhi::Format m_NVRHIFormat;
    bool m_isCubemap;
    TextureDimension m_texDim;
};

nvrhi::TextureHandle CreateDDSTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName)
{
    PROFILE_FUNCTION();

    DDSFile ddsFile;
    const DDSFile::Result result = ddsFile.Load({ (const uint8_t*)data, (const uint8_t*)data + nbBytes });
    if (result != DDSFile::Result::Success)
    {
        LOG_DEBUG("Failed to load DDS file: %s", EnumUtils::ToString(result));
        assert(0);
    }

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = ddsFile.m_NVRHIFormat;
    textureDesc.width = ddsFile.m_width;
    textureDesc.height = ddsFile.m_height;
    textureDesc.mipLevels = ddsFile.m_mipCount;
    textureDesc.debugName = debugName;
    textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;

    nvrhi::TextureHandle newTexture = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);

    for (uint32_t mip = 0; mip < ddsFile.m_mipCount; ++mip)
    {
        const DDSFile::ImageData* imageData = ddsFile.GetImageData(mip);
        assert(imageData);

        commandList->writeTexture(newTexture, 0, mip, imageData->m_mem, imageData->m_memPitch);
    }

    commandList->setPermanentTextureState(newTexture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    return newTexture;
}

bool IsSTBImage(const void* data, uint32_t nbBytes)
{
    return !!stbi_info_from_memory((const stbi_uc*)data, (int)nbBytes, nullptr, nullptr, nullptr);
}

nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB)
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
    const uint32_t bytesPerPixelCheck = nvrhi::getFormatInfo(format).bytesPerBlock;
    assert(bytesPerPixel == bytesPerPixelCheck);

    commandList->writeTexture(newTexture, 0, 0, bitmap, static_cast<size_t>(width * bytesPerPixel), 0);
    commandList->setPermanentTextureState(newTexture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    stbi_image_free(bitmap);

    return newTexture;
}
