#include "CommonResources.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "MathUtilities.h"

#pragma warning(push)
#pragma warning (disable: 4244) // conversion warning
#pragma warning (disable: 4267) // conversion warning

const nvrhi::BlendState::RenderTarget CommonResources::BlendOpaque =
{
    false,                    // blendEnable
    nvrhi::BlendFactor::One,  // srcBlend
    nvrhi::BlendFactor::Zero, // destBlend
    nvrhi::BlendOp::Add,      // blendOp
    nvrhi::BlendFactor::One,  // srcBlendAlpha
    nvrhi::BlendFactor::Zero, // destBlendAlpha
    nvrhi::BlendOp::Add,      // blendOpAlpha
    nvrhi::ColorMask::All     // colorWriteMask
};

const nvrhi::BlendState::RenderTarget CommonResources::BlendAlpha =
{
    true,                            // blendEnable
    nvrhi::BlendFactor::SrcAlpha,    // srcBlend
    nvrhi::BlendFactor::InvSrcAlpha, // destBlend
    nvrhi::BlendOp::Add,             // blendOp
    nvrhi::BlendFactor::Zero,        // srcBlendAlpha
    nvrhi::BlendFactor::One,         // destBlendAlpha
    nvrhi::BlendOp::Add,             // blendOpAlpha
    nvrhi::ColorMask::All            // colorWriteMask
};

const nvrhi::BlendState::RenderTarget CommonResources::BlendAdditive =
{
    true,                    // blendEnable
    nvrhi::BlendFactor::One, // srcBlend
    nvrhi::BlendFactor::One, // destBlend
    nvrhi::BlendOp::Add,     // blendOp
    nvrhi::BlendFactor::One, // srcBlendAlpha
    nvrhi::BlendFactor::Zero, // destBlendAlpha
    nvrhi::BlendOp::Add,     // blendOpAlpha
    nvrhi::ColorMask::All    // colorWriteMask
};


const nvrhi::BlendState::RenderTarget CommonResources::BlendIMGUI =
{
    true,                             // blendEnable
    nvrhi::BlendFactor::SrcAlpha,     // srcBlend
    nvrhi::BlendFactor::InvSrcAlpha,  // destBlend
    nvrhi::BlendOp::Add,              // blendOp
    nvrhi::BlendFactor::InvSrcAlpha,  // srcBlendAlpha
    nvrhi::BlendFactor::Zero,         // destBlendAlpha
    nvrhi::BlendOp::Add,              // blendOpAlpha
    nvrhi::ColorMask::All             // colorWriteMask
};

const nvrhi::BlendState::RenderTarget CommonResources::BlendDebugDraw =
{
    true,                             // blendEnable
    nvrhi::BlendFactor::SrcAlpha,     // srcBlend
    nvrhi::BlendFactor::InvSrcAlpha,  // destBlend
    nvrhi::BlendOp::Add,              // blendOp
    nvrhi::BlendFactor::One,          // srcBlendAlpha
    nvrhi::BlendFactor::Zero,         // destBlendAlpha
    nvrhi::BlendOp::Add,              // blendOpAlpha
    nvrhi::ColorMask::All             // colorWriteMask
};

const nvrhi::RasterState CommonResources::CullNone =
{
    nvrhi::RasterFillMode::Solid, // fillMode
    nvrhi::RasterCullMode::None, // cullMode
    true // frontCounterClockwise
};

const nvrhi::RasterState CommonResources::CullClockwise =
{
    nvrhi::RasterFillMode::Solid, // fillMode
    nvrhi::RasterCullMode::Back, // cullMode
    true // frontCounterClockwise
};

const nvrhi::RasterState CommonResources::CullCounterClockwise =
{
    nvrhi::RasterFillMode::Solid, // fillMode
    nvrhi::RasterCullMode::Front, // cullMode
    true // frontCounterClockwise
};

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

    static const nvrhi::VertexAttributeDesc s_ImguiLayout[] = {
        { "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,pos), sizeof(ImDrawVert), false },
        { "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,uv),  sizeof(ImDrawVert), false },
        { "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert,col), sizeof(ImDrawVert), false },
    };

    static const nvrhi::VertexAttributeDesc s_DebugDrawLayout[] =
    {
        { "POSITION", nvrhi::Format::RGB32_FLOAT, 1, 0, 0, 36, false },
        { "TEXCOORD", nvrhi::Format::RGB32_FLOAT, 1, 0, 12, 36, false },
        { "COLOR",    nvrhi::Format::RGB32_FLOAT, 1, 0, 24, 36, false }
    };

    static const nvrhi::VertexAttributeDesc s_GPUCullingLayout[] =
    {
        { "INSTANCE_START_LOCATION", nvrhi::Format::R32_UINT, 1, 0, 0, sizeof(uint32_t), true }
    };

    // VS is not needed in 'createInputLayout', there are no separate IL objects in DX12
    nvrhi::IShader* dummyVS = nullptr;

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
    g_CommonResources.IMGUILayout = device->createInputLayout(s_ImguiLayout, (uint32_t)std::size(s_ImguiLayout), dummyVS);
    g_CommonResources.DebugDrawLayout = device->createInputLayout(s_DebugDrawLayout, (uint32_t)std::size(s_DebugDrawLayout), dummyVS);
    g_CommonResources.GPUCullingLayout = device->createInputLayout(s_GPUCullingLayout, (uint32_t)std::size(s_GPUCullingLayout), dummyVS);
}

static void CreateDefaultDepthStencilStates()
{
    PROFILE_FUNCTION();

    enum class EDepthStencilState { None, Read, Write };

    nvrhi::DepthStencilState desc;
    auto UpdateDesc = [&](EDepthStencilState depthState, EDepthStencilState stencilState)
        {
            desc.depthTestEnable = depthState != EDepthStencilState::None;
            desc.depthWriteEnable = depthState == EDepthStencilState::Write;
            desc.depthFunc = Graphic::kInversedDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual;

            desc.stencilEnable = stencilState != EDepthStencilState::None;
            desc.stencilReadMask = stencilState == EDepthStencilState::Read ? 0xFF : 0x00;
            desc.stencilWriteMask = stencilState == EDepthStencilState::Write ? 0xFF : 0x00;
        };

    static const EDepthStencilState kDepthStencilStates[][2] =
    {
        { EDepthStencilState::None, EDepthStencilState::None },
        { EDepthStencilState::None, EDepthStencilState::Read },
        { EDepthStencilState::None, EDepthStencilState::Write },
        { EDepthStencilState::Read, EDepthStencilState::None },
        { EDepthStencilState::Read, EDepthStencilState::Read },
        { EDepthStencilState::Read, EDepthStencilState::Write },
        { EDepthStencilState::Write, EDepthStencilState::None },
        { EDepthStencilState::Write, EDepthStencilState::Read },
        { EDepthStencilState::Write, EDepthStencilState::Write }
    };

    nvrhi::DepthStencilState* pStates[] =
    {
        &g_CommonResources.DepthNoneStencilNone,
        &g_CommonResources.DepthNoneStencilRead,
        &g_CommonResources.DepthNoneStencilWrite,
        &g_CommonResources.DepthReadStencilNone,
        &g_CommonResources.DepthReadStencilRead,
        &g_CommonResources.DepthReadStencilWrite,
        &g_CommonResources.DepthWriteStencilNone,
        &g_CommonResources.DepthWriteStencilRead,
        &g_CommonResources.DepthWriteStencilWrite
    };

    static_assert(std::size(kDepthStencilStates) == std::size(pStates), "Mismatched array sizes");

    for (uint32_t i = 0; i < std::size(kDepthStencilStates); ++i)
    {
        UpdateDesc(kDepthStencilStates[i][0], kDepthStencilStates[i][1]);
        *pStates[i] = desc;
    }
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
}

#pragma warning(pop)
