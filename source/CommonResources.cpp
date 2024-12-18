#include "CommonResources.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "MathUtilities.h"

static void CreateDefaultTexture(
    std::string_view name,
    Texture& destTex,
    nvrhi::Format format,
    uint32_t data,
    uint32_t arraySize,
    bool bUAV)
{
    PROFILE_FUNCTION();

    nvrhi::TextureDesc textureDesc;
    textureDesc.arraySize = arraySize;
    textureDesc.dimension = arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
    textureDesc.format = format;
    textureDesc.debugName = name;
    textureDesc.isUAV = bUAV;
    textureDesc.initialState = bUAV ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;
    destTex.LoadFromMemory(&data, textureDesc);
}

static void CreateDefaultBuffer(
    std::string_view name,
    nvrhi::BufferHandle& destBuffer,
    uint32_t byteSize,
    uint32_t structStride,
    bool bUAV,
    bool bRaw)
{
    PROFILE_FUNCTION();

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::BufferDesc desc;
    desc.byteSize = byteSize;
    desc.structStride = bRaw ? 0 : structStride; // if non-zero it's structured
    desc.debugName = name;
    desc.canHaveUAVs = bUAV;
    desc.canHaveRawViews = bRaw;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;

    // TODO: support these
    //desc.format = nvrhi::Format::UNKNOWN; // for typed buffer views
    //desc.canHaveTypedViews = false;
    //desc.isVertexBuffer = false;
    //desc.isIndexBuffer = false;
    //desc.isConstantBuffer = false;
    //desc.isDrawIndirectArgs = false;
    //desc.isAccelStructBuildInput = false;
    //desc.isAccelStructStorage = false;
    //desc.isShaderBindingTable = false;

    destBuffer = device->createBuffer(desc);
}

static void CreateDefaultSamplers()
{
    PROFILE_FUNCTION();

    auto CreateSampler = [](bool bMinFilter, bool bMagFilter, bool bMipFilter, nvrhi::SamplerAddressMode addressMode, nvrhi::SamplerReductionType reductionType, uint32_t maxAnisotropy)
        {
			nvrhi::SamplerDesc samplerDesc;
			samplerDesc.setMinFilter(bMinFilter);
			samplerDesc.setMagFilter(bMagFilter);
			samplerDesc.setMipFilter(bMipFilter);
			samplerDesc.setAllAddressModes(addressMode);
            samplerDesc.setReductionType(reductionType);
			samplerDesc.setMaxAnisotropy(maxAnisotropy);

			return g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);
        };

	g_CommonResources.PointClampSampler                = CreateSampler(false, false, false, nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Standard, 1);
	g_CommonResources.LinearClampSampler               = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Standard, 1);
	g_CommonResources.LinearWrapSampler                = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Wrap,   nvrhi::SamplerReductionType::Standard, 1);
	g_CommonResources.AnisotropicClampSampler          = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Standard, 16);
	g_CommonResources.AnisotropicWrapSampler           = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Wrap,   nvrhi::SamplerReductionType::Standard, 16);
	g_CommonResources.AnisotropicBorderSampler         = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Border, nvrhi::SamplerReductionType::Standard, 16);
	g_CommonResources.AnisotropicMirrorSampler         = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Mirror, nvrhi::SamplerReductionType::Standard, 16);
	g_CommonResources.LinearClampComparisonLessSampler = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Comparison, 1);
	g_CommonResources.PointClampComparisonLessSampler  = CreateSampler(false, false, false, nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Comparison, 1);
    g_CommonResources.LinearClampMinReductionSampler   = CreateSampler(true, true, true   , nvrhi::SamplerAddressMode::Clamp,  nvrhi::SamplerReductionType::Minimum, 1);
}

static void CreateDefaultInputLayouts()
{
    PROFILE_FUNCTION();

    static const nvrhi::VertexAttributeDesc s_GPUCullingLayout[] =
    {
        { "INSTANCE_START_LOCATION", nvrhi::Format::R32_UINT, 1, 0, 0, sizeof(uint32_t), true }
    };

    // VS is not needed in 'createInputLayout', there are no separate IL objects in DX12
    nvrhi::IShader* dummyVS = nullptr;

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
    g_CommonResources.GPUCullingLayout = device->createInputLayout(s_GPUCullingLayout, (uint32_t)std::size(s_GPUCullingLayout), dummyVS);
}

static void CreateDefaultDepthStencilStates()
{
    PROFILE_FUNCTION();

    auto CreateDepthStencilstate = [](bool bDepthTestEnable, bool bDepthWrite, nvrhi::ComparisonFunc depthFunc, bool bStencilEnable, uint8_t stencilReadMask, uint8_t stencilWriteMask)
        {
			nvrhi::DepthStencilState desc;
			desc.depthTestEnable = bDepthTestEnable;
			desc.depthWriteEnable = bDepthWrite;
			desc.depthFunc = depthFunc;
			desc.stencilEnable = bStencilEnable;
			desc.stencilReadMask = stencilReadMask;
			desc.stencilWriteMask = stencilWriteMask;
			return desc;
        };

	g_CommonResources.DepthNoneStencilNone   = CreateDepthStencilstate(false, false, nvrhi::ComparisonFunc::Always, false, 0, 0);
	g_CommonResources.DepthNoneStencilRead   = CreateDepthStencilstate(false, false, nvrhi::ComparisonFunc::Always, true, 0xFF, 0);
	g_CommonResources.DepthNoneStencilWrite  = CreateDepthStencilstate(false, false, nvrhi::ComparisonFunc::Always, true, 0, 0xFF);
	g_CommonResources.DepthReadStencilNone   = CreateDepthStencilstate(true, false, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, false, 0, 0);
	g_CommonResources.DepthReadStencilRead   = CreateDepthStencilstate(true, false, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, true, 0xFF, 0);
	g_CommonResources.DepthReadStencilWrite  = CreateDepthStencilstate(true, false, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, true, 0, 0xFF);
	g_CommonResources.DepthWriteStencilNone  = CreateDepthStencilstate(true, true, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, false, 0, 0);
	g_CommonResources.DepthWriteStencilRead  = CreateDepthStencilstate(true, true, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, true, 0xFF, 0);
	g_CommonResources.DepthWriteStencilWrite = CreateDepthStencilstate(true, true, Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual, true, 0, 0xFF);
}

static void CreateDefaultBlendModes()
{
    PROFILE_FUNCTION();

    auto CreateBlendState = [](bool bBlendEnable, nvrhi::BlendFactor srcBlend, nvrhi::BlendFactor destBlend, nvrhi::BlendOp blendOp, nvrhi::BlendFactor srcBlendAlpha, nvrhi::BlendFactor destBlendAlpha, nvrhi::BlendOp blendOpAlpha)
        {
            nvrhi::BlendState::RenderTarget desc;
            desc.blendEnable = bBlendEnable;
            desc.srcBlend = srcBlend;
            desc.destBlend = destBlend;
            desc.blendOp = blendOp;
            desc.srcBlendAlpha = srcBlendAlpha;
            desc.destBlendAlpha = destBlendAlpha;
            desc.blendOpAlpha = blendOpAlpha;
            desc.colorWriteMask = nvrhi::ColorMask::All;
            return desc;
        };

    g_CommonResources.BlendOpaque             = CreateBlendState(false, nvrhi::BlendFactor::One, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add, nvrhi::BlendFactor::One, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add);
    g_CommonResources.BlendModulate           = CreateBlendState(true, nvrhi::BlendFactor::DstColor, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add, nvrhi::BlendFactor::DstAlpha, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add);
    g_CommonResources.BlendAlpha              = CreateBlendState(true, nvrhi::BlendFactor::SrcAlpha, nvrhi::BlendFactor::InvSrcAlpha, nvrhi::BlendOp::Add, nvrhi::BlendFactor::Zero, nvrhi::BlendFactor::One, nvrhi::BlendOp::Add);
    g_CommonResources.BlendAdditive           = CreateBlendState(true, nvrhi::BlendFactor::One, nvrhi::BlendFactor::One, nvrhi::BlendOp::Add, nvrhi::BlendFactor::One, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add);
    g_CommonResources.BlendAlphaAdditive      = CreateBlendState(true, nvrhi::BlendFactor::SrcAlpha, nvrhi::BlendFactor::One, nvrhi::BlendOp::Add, nvrhi::BlendFactor::Zero, nvrhi::BlendFactor::One, nvrhi::BlendOp::Add);
	g_CommonResources.BlendDestAlpha          = CreateBlendState(true, nvrhi::BlendFactor::DstAlpha, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add, nvrhi::BlendFactor::DstAlpha, nvrhi::BlendFactor::Zero, nvrhi::BlendOp::Add);
	g_CommonResources.BlendPremultipliedAlpha = CreateBlendState(true, nvrhi::BlendFactor::One, nvrhi::BlendFactor::InvSrcAlpha, nvrhi::BlendOp::Add, nvrhi::BlendFactor::One, nvrhi::BlendFactor::InvSrcAlpha, nvrhi::BlendOp::Add);
}

static void CreateDefaultRasterStates()
{
	PROFILE_FUNCTION();

	auto CreateRasterState = [](nvrhi::RasterCullMode cullMode)
		{
			nvrhi::RasterState desc;
			desc.fillMode = nvrhi::RasterFillMode::Solid;
			desc.cullMode = cullMode;
			desc.frontCounterClockwise = true;
			return desc;
		};

	g_CommonResources.CullNone             = CreateRasterState(nvrhi::RasterCullMode::None);
	g_CommonResources.CullClockwise        = CreateRasterState(nvrhi::RasterCullMode::Back);
	g_CommonResources.CullCounterClockwise = CreateRasterState(nvrhi::RasterCullMode::Front);
}

void CommonResources::Initialize()
{
    PROFILE_FUNCTION();

    CreateDefaultTexture("Black 2D Texture",       BlackTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture("White 2D Texture",       WhiteTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 1.0f, 1.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture("Dummy UAV 2D Texture",   DummyUAV2DTexture,  nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, true /*bUAV*/);
    CreateDefaultTexture("R8 UInt Max 2D Texture", R8UIntMax2DTexture, nvrhi::Format::R8_UINT, UINT8_MAX, 1, false /*bUAV*/);

    CreateDefaultBuffer("DummyUIntStructuredBuffer", DummyUIntStructuredBuffer, sizeof(uint32_t), sizeof(uint32_t), true /*bUAV*/, false /*bRaw*/);
	CreateDefaultBuffer("DummyRawBuffer", DummyRawBuffer, sizeof(uint32_t), 0, true /*bUAV*/, true /*bRaw*/);

    CreateDefaultSamplers();
    CreateDefaultInputLayouts();
    CreateDefaultDepthStencilStates();
    CreateDefaultBlendModes();
    CreateDefaultRasterStates();
}
