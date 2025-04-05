#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "Visual.h"

class CommonResources
{
public:
    SingletonFunctionsSimple(CommonResources);

    nvrhi::InputLayoutHandle m_UncompressedRawVertexFormatInputLayoutHandle;

    nvrhi::BufferHandle UnitSphereVertexBuffer;
    nvrhi::BufferHandle UnitSphereIndexBuffer;

    // Textures
    Texture BlackTexture;
    Texture WhiteTexture;
    Texture DefaultRoughnessMetallicTexture;
    Texture DefaultNormalTexture;
    Texture DummyUAV2DTexture;
    Texture R8UIntMax2DTexture;
    Texture BlueNoise;

    // Materials
    Material DefaultMaterial;

    // Buffers
    nvrhi::BufferHandle DummyUIntStructuredBuffer;
    nvrhi::BufferHandle DummyRawBuffer;

    // Samplers
    nvrhi::SamplerHandle PointClampSampler;
    nvrhi::SamplerHandle LinearClampSampler;
    nvrhi::SamplerHandle LinearClampMinReductionSampler;
    nvrhi::SamplerHandle LinearWrapSampler;
    nvrhi::SamplerHandle AnisotropicClampSampler;
    nvrhi::SamplerHandle AnisotropicWrapSampler;
    nvrhi::SamplerHandle AnisotropicBorderSampler;
    nvrhi::SamplerHandle AnisotropicMirrorSampler;
    nvrhi::SamplerHandle PointClampComparisonLessSampler;
    nvrhi::SamplerHandle LinearClampComparisonLessSampler;

    // Blend States
    nvrhi::BlendState::RenderTarget BlendOpaque;
    nvrhi::BlendState::RenderTarget BlendModulate;
    nvrhi::BlendState::RenderTarget BlendAlpha;
    nvrhi::BlendState::RenderTarget BlendAdditive;
    nvrhi::BlendState::RenderTarget BlendAlphaAdditive;
    nvrhi::BlendState::RenderTarget BlendDestAlpha;
    nvrhi::BlendState::RenderTarget BlendPremultipliedAlpha;

    // Raster States
    nvrhi::RasterState CullNone;
    nvrhi::RasterState CullClockwise;
    nvrhi::RasterState CullCounterClockwise;

    // Depth Stencil States
    nvrhi::DepthStencilState DepthNoneStencilNone;
    nvrhi::DepthStencilState DepthNoneStencilRead;
    nvrhi::DepthStencilState DepthNoneStencilWrite;
    nvrhi::DepthStencilState DepthReadStencilNone;
    nvrhi::DepthStencilState DepthReadStencilRead;
    nvrhi::DepthStencilState DepthReadStencilWrite;
    nvrhi::DepthStencilState DepthWriteStencilNone;
    nvrhi::DepthStencilState DepthWriteStencilRead;
    nvrhi::DepthStencilState DepthWriteStencilWrite;

private:
    void Initialize();

    friend class Graphic;
};
#define g_CommonResources CommonResources::GetInstance()
