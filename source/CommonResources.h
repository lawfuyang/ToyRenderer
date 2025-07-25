#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "Visual.h"

struct DefaultGeometry
{
    uint32_t m_NumVertices;
    uint32_t m_NumIndices;
    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;
};

class CommonResources
{
public:
    SingletonFunctionsSimple(CommonResources);

    nvrhi::InputLayoutHandle m_UncompressedRawVertexFormatInputLayoutHandle;

    DefaultGeometry UnitSphere;

    // Textures
    Texture BlackTexture;
    Texture BlackTexture2DArray;
    Texture WhiteTexture;
    Texture DefaultRoughnessMetallicTexture;
    Texture DefaultNormalTexture;
    Texture DummyUAV2DTexture;
    Texture R8UIntMax2DTexture;
    Texture BlueNoise;

    nvrhi::TextureHandle DummyMinMipTexture;
    nvrhi::SamplerFeedbackTextureHandle DummySamplerFeedbackTexture;
    uint32_t DummyMinMipIndexInTable = UINT_MAX;
    uint32_t DummySamplerFeedbackIndexInTable = UINT_MAX;

    // Materials
    Material DefaultMaterial;

    // Buffers
    nvrhi::BufferHandle DummyUIntStructuredBuffer;
    nvrhi::BufferHandle DummyRawBuffer;

    // Samplers
    nvrhi::SamplerHandle PointClampSampler;
    nvrhi::SamplerHandle LinearClampSampler;
    nvrhi::SamplerHandle LinearClampMinReductionSampler;
    nvrhi::SamplerHandle LinearClampMaxReductionSampler;
    nvrhi::SamplerHandle LinearWrapSampler;
    nvrhi::SamplerHandle AnisotropicClampSampler;
    nvrhi::SamplerHandle AnisotropicWrapSampler;
    nvrhi::SamplerHandle AnisotropicBorderSampler;
    nvrhi::SamplerHandle AnisotropicMirrorSampler;
    nvrhi::SamplerHandle PointClampComparisonLessSampler;
    nvrhi::SamplerHandle LinearClampComparisonLessSampler;
    nvrhi::SamplerHandle AnisotropicClampMaxReductionSampler;
    nvrhi::SamplerHandle AnisotropicWrapMaxReductionSampler;

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
    nvrhi::RasterState CullBackFace;

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
