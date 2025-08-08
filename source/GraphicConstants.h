#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

namespace GraphicConstants
{
    static constexpr uint32_t kMaxTextureDimension = 16384;
    static constexpr uint32_t kMaxTextureMips = std::bit_width(kMaxTextureDimension);
    static constexpr uint32_t kTiledResourceSizeInBytes = KB_TO_BYTES(64);
    static constexpr uint32_t kMaxThreadGroupsPerDimension = 65535;
    static constexpr uint32_t kMaxNumMeshLODs = 8;

    static constexpr uint32_t kStencilBit_Opaque = 0x0;
    static constexpr uint32_t kStencilBit_Sky = 0x1;

    static constexpr bool kFrontCCW = true;
    static constexpr bool kInversedDepthBuffer = true;
    static constexpr bool kInfiniteDepthBuffer = true;

    static constexpr float kNearDepth = kInversedDepthBuffer ? 1.0f : 0.0f;
    static constexpr float kFarDepth = 1.0f - kNearDepth;
    static constexpr float kDefaultCameraNearPlane = 0.1f;

    static constexpr nvrhi::Format kGBufferAFormat = nvrhi::Format::RGBA32_UINT;
    static constexpr nvrhi::Format kGBufferMotionFormat = nvrhi::Format::RG16_FLOAT;
    static constexpr nvrhi::Format kDepthStencilFormat = nvrhi::Format::D24S8;
    static constexpr nvrhi::Format kDepthBufferCopyFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kHZBFormat = nvrhi::Format::R16_FLOAT;
    static constexpr nvrhi::Format kIndexBufferFormat = nvrhi::Format::R32_UINT;
    static constexpr nvrhi::Format kLightingOutputFormat = nvrhi::Format::R11G11B10_FLOAT;
    static constexpr nvrhi::Format kSSAOOutputFormat = nvrhi::Format::R8_UINT;
    static constexpr nvrhi::Format kMinMipFormat = nvrhi::Format::R8_UINT;

    using IndexBufferFormat_t = uint32_t;

    static constexpr uint32_t kSrvUavCbvBindlessLayoutCapacity = 1024; // NOTE: increase if needed
}
