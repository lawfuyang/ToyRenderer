#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "Visual.h"

class CommonResources
{
public:
    SingletonFunctionsSimple(CommonResources);

    // Textures
    Texture BlackTexture;
    Texture WhiteTexture;
    Texture DummyUAV2DTexture;
    Texture R8UIntMax2DTexture;

    // Materials
    Material DefaultMaterial;

    // Buffers
    nvrhi::BufferHandle DummyUintStructuredBuffer;

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

    // Input Layouts
    nvrhi::InputLayoutHandle IMGUILayout;
    nvrhi::InputLayoutHandle DebugDrawLayout;
    nvrhi::InputLayoutHandle GPUCullingLayout;

    // Blend States
    static const nvrhi::BlendState::RenderTarget BlendOpaque;
    static const nvrhi::BlendState::RenderTarget BlendModulate;
    static const nvrhi::BlendState::RenderTarget BlendAlpha;
    static const nvrhi::BlendState::RenderTarget BlendAdditive;
    static const nvrhi::BlendState::RenderTarget BlendAlphaAdditive;
    static const nvrhi::BlendState::RenderTarget BlendDestAlpha;
    static const nvrhi::BlendState::RenderTarget BlendPremultipliedAlpha;
    static const nvrhi::BlendState::RenderTarget BlendIMGUI;
    static const nvrhi::BlendState::RenderTarget BlendDebugDraw;

    // Raster States
    static const nvrhi::RasterState CullNone;
    static const nvrhi::RasterState CullClockwise;
    static const nvrhi::RasterState CullCounterClockwise;

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
