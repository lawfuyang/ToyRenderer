#include "CommonResources.h"

#include "extern/imgui/imgui.h"
#include "extern/amd/FidelityFX/samples/thirdparty/samplercpp/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"

#include "Engine.h"
#include "Graphic.h"
#include "MathUtilities.h"

#include "shaders/ShaderInterop.h"

static void CreateUncompressedRawVertexFormatInputLayoutHandle()
{
    static const nvrhi::VertexAttributeDesc s_Layout[] =
    {
        { "POSITION" , nvrhi::Format::RGB32_FLOAT, 1, 0, offsetof(UncompressedRawVertexFormat, m_Position) , sizeof(UncompressedRawVertexFormat), false },
        { "NORMAL" , nvrhi::Format::RGB32_FLOAT, 1, 0, offsetof(UncompressedRawVertexFormat, m_Normal) , sizeof(UncompressedRawVertexFormat), false },
        { "TEXCOORD" , nvrhi::Format::RG32_FLOAT, 1, 0, offsetof(UncompressedRawVertexFormat, m_TexCoord) , sizeof(UncompressedRawVertexFormat), false }
    };

    // VS is not needed in 'createInputLayout', there are no separate IL objects in DX12
    nvrhi::IShader* dummyVS = nullptr;
    g_CommonResources.m_UncompressedRawVertexFormatInputLayoutHandle = g_Graphic.m_NVRHIDevice->createInputLayout(s_Layout, std::size(s_Layout), dummyVS);
}

static void ReverseWinding(std::vector<Graphic::IndexBufferFormat_t>& indices, std::vector<UncompressedRawVertexFormat>& vertices)
{
    assert((indices.size() % 3) == 0);
    for (auto it = indices.begin(); it != indices.end(); it += 3)
    {
        std::swap(*it, *(it + 2));
    }

    for (UncompressedRawVertexFormat& v : vertices)
    {
        v.m_TexCoord.x = (1.0f - v.m_TexCoord.x);
    }
}

static void CreateUnitSphereMesh()
{
    PROFILE_FUNCTION();

    const float kRadius = 0.5f;
    const uint32_t kTessellation = 6;
    const uint32_t kVerticalSegments = kTessellation;
    const uint32_t kHorizontalSegments = kTessellation * 2;

    std::vector<UncompressedRawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    // Create rings of vertices at progressively higher latitudes.
    for (uint32_t i = 0; i <= kVerticalSegments; i++)
    {
        const float v = 1 - float(i) / float(kVerticalSegments);

        float dy, dxz;
        const float latitude = (float(i) * std::numbers::pi / float(kVerticalSegments)) - (std::numbers::pi * 0.5f);
        ScalarSinCos(dy, dxz, latitude);

        // Create a single ring of vertices at this latitude.
        for (uint32_t j = 0; j <= kHorizontalSegments; j++)
        {
            const float u = float(j) / float(kHorizontalSegments);

            float dx, dz;
            const float longitude = float(j) * (std::numbers::pi * 2) / float(kHorizontalSegments);
            ScalarSinCos(dx, dz, longitude);

            dx *= dxz;
            dz *= dxz;

            const Vector3 normal{ dx, dy, dz };
            const Vector4 tangent{ -dz, 0, dx, 1 };
            const Vector2 textureCoordinate{ u, v };

            vertices.push_back(UncompressedRawVertexFormat{ { normal * kRadius }, normal, textureCoordinate });
        }
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    const uint32_t stride = kHorizontalSegments + 1;

    for (uint32_t i = 0; i < kVerticalSegments; i++)
    {
        for (uint32_t j = 0; j <= kHorizontalSegments; j++)
        {
            const uint32_t nextI = i + 1;
            const uint32_t nextJ = (j + 1) % stride;

            indices.push_back(i * stride + j);
            indices.push_back(nextI * stride + j);
            indices.push_back(i * stride + nextJ);

            indices.push_back(i * stride + nextJ);
            indices.push_back(nextI * stride + j);
            indices.push_back(nextI * stride + nextJ);
        }
    }

    // Build RH above
    ReverseWinding(indices, vertices);

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::BufferDesc vbDesc;
    vbDesc.byteSize = vertices.size() * sizeof(UncompressedRawVertexFormat);
    vbDesc.debugName = "Unit Sphere Vertex Buffer";
    vbDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    vbDesc.format = nvrhi::Format::RG32_FLOAT;
    vbDesc.isVertexBuffer = true;
    g_CommonResources.UnitSphere.m_VertexBuffer = device->createBuffer(vbDesc);

    nvrhi::BufferDesc ibDesc;
    ibDesc.byteSize = indices.size() * sizeof(Graphic::IndexBufferFormat_t);
    ibDesc.debugName = "Unit Sphere Index Buffer";
    ibDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    ibDesc.format = nvrhi::Format::R32_UINT;
    ibDesc.isIndexBuffer = true;
    g_CommonResources.UnitSphere.m_IndexBuffer = device->createBuffer(ibDesc);

    g_CommonResources.UnitSphere.m_NumVertices = vertices.size();
    g_CommonResources.UnitSphere.m_NumIndices = indices.size();

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Create Unit Sphere Mesh");

    commandList->writeBuffer(g_CommonResources.UnitSphere.m_VertexBuffer, vertices.data(), vbDesc.byteSize);
    commandList->writeBuffer(g_CommonResources.UnitSphere.m_IndexBuffer, indices.data(), ibDesc.byteSize);
}

static void CreateDefaultTextures()
{
    PROFILE_FUNCTION();

    auto CreateDefaultTexture = [](
        Texture& outTex,
        std::string_view name,
        nvrhi::Format format,
        uint32_t data,
        uint32_t arraySize,
        bool bUAV)
        {
            PROFILE_SCOPED(name.data());

            nvrhi::TextureDesc textureDesc;
            textureDesc.arraySize = arraySize;
            textureDesc.dimension = arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
            textureDesc.format = format;
            textureDesc.debugName = name;
            textureDesc.isUAV = bUAV;
            textureDesc.initialState = bUAV ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;

            outTex.LoadFromMemory(&data, textureDesc);
        };

    CreateDefaultTexture(g_CommonResources.BlackTexture                   , "Black 2D Texture", nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.BlackTexture2DArray            , "Black 2D Texture2DArray", nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 2, false /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.WhiteTexture                   , "White 2D Texture", nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 1.0f, 1.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.DefaultRoughnessMetallicTexture, "Default Roughness Metallic Texture", nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 1.0f, 0.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.DefaultNormalTexture           , "Default Normal Texture", nvrhi::Format::RGBA8_UNORM, Color{ 0.5f, 0.5f, 1.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.DummyUAV2DTexture              , "Dummy UAV 2D Texture", nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, true /*bUAV*/);
    CreateDefaultTexture(g_CommonResources.R8UIntMax2DTexture             , "R8 UInt Max 2D Texture", nvrhi::Format::R8_UINT, UINT8_MAX, 1, false /*bUAV*/);

    // blue noise
    // TODO: Nvidia STBN
    {
        static const uint32_t kBlueNoiseDimensions = 128;

        // jank solution to prevent blowing up the stack
        struct BlueNoiseData
        {
            std::byte m_Data[kBlueNoiseDimensions][kBlueNoiseDimensions][4];
        };
        std::unique_ptr<BlueNoiseData> blueNoiseData = std::make_unique<BlueNoiseData>();

        for (int x = 0; x < kBlueNoiseDimensions; ++x)
        {
            for (int y = 0; y < kBlueNoiseDimensions; ++y)
            {
                const float f0 = samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 0);
                const float f1 = samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 1);
                const float f2 = samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 2);
                const float f3 = samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 3);

                blueNoiseData->m_Data[x][y][0] = static_cast<std::byte>(f0 * UINT8_MAX);
                blueNoiseData->m_Data[x][y][1] = static_cast<std::byte>(f1 * UINT8_MAX);
                blueNoiseData->m_Data[x][y][2] = static_cast<std::byte>(f2 * UINT8_MAX);
                blueNoiseData->m_Data[x][y][3] = static_cast<std::byte>(f3 * UINT8_MAX);
            }
        }

        nvrhi::TextureDesc desc;
        desc.width = kBlueNoiseDimensions;
        desc.height = kBlueNoiseDimensions;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.debugName = "Blue Noise";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        g_CommonResources.BlueNoise.LoadFromMemory(blueNoiseData->m_Data, desc);
    }
}

static void CreateDefaultBuffers()
{
    PROFILE_FUNCTION();

    auto CreateDefaultBuffer = [](
        std::string_view name,
        uint32_t byteSize,
        uint32_t structStride,
        bool bUAV,
        bool bRaw)
        {
            PROFILE_SCOPED(name.data());

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

            return device->createBuffer(desc);
        };

    g_CommonResources.DummyUIntStructuredBuffer = CreateDefaultBuffer("DummyUIntStructuredBuffer", sizeof(uint32_t), sizeof(uint32_t), true /*bUAV*/, false /*bRaw*/);
    g_CommonResources.DummyRawBuffer            = CreateDefaultBuffer("DummyRawBuffer", sizeof(uint32_t), 0, true /*bUAV*/, true /*bRaw*/);
}

static void CreateDefaultSamplers()
{
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

static void CreateDefaultDepthStencilStates()
{
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

    g_CommonResources.CullBackFace = Graphic::kFrontCCW ? g_CommonResources.CullClockwise : g_CommonResources.CullCounterClockwise;
}

void CommonResources::Initialize()
{
    PROFILE_FUNCTION();

    CreateUncompressedRawVertexFormatInputLayoutHandle();
    CreateUnitSphereMesh();
    CreateDefaultTextures();
    CreateDefaultBuffers();
    CreateDefaultSamplers();
    CreateDefaultDepthStencilStates();
    CreateDefaultBlendModes();
    CreateDefaultRasterStates();
}
