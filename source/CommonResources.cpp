#include "CommonResources.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "Graphic.h"
#include "MathUtilities.h"

#include "shaders/shared/RawVertexFormat.h"
#include "shaders/shared/MaterialData.h"

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
    bool bUAV)
{
    PROFILE_FUNCTION();

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    nvrhi::BufferDesc desc;
    desc.byteSize = byteSize;
    desc.structStride = structStride; // if non-zero it's structured
    desc.debugName = name;
    desc.canHaveUAVs = bUAV;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;

    // TODO: support these
    //desc.format = nvrhi::Format::UNKNOWN; // for typed buffer views
    //desc.canHaveTypedViews = false;
    //desc.canHaveRawViews = false;
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

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.setMaxAnisotropy(16);

    samplerDesc.setAllFilters(false);
    g_CommonResources.PointClampSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllFilters(true);
    g_CommonResources.LinearClampSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
    g_CommonResources.LinearWrapSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    g_CommonResources.AnisotropicClampSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
    g_CommonResources.AnisotropicWrapSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Border);
    g_CommonResources.AnisotropicBorderSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Mirror);
    g_CommonResources.AnisotropicMirrorSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.setAllFilters(true);
    samplerDesc.setReductionType(nvrhi::SamplerReductionType::Comparison);
    samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    g_CommonResources.LinearClampComparisonLessSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);

    samplerDesc.mipFilter = false;
    g_CommonResources.PointClampComparisonLessSampler = g_Graphic.m_NVRHIDevice->createSampler(samplerDesc);
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

static void ReverseWinding(std::vector<Graphic::IndexBufferFormat_t>& indices, std::vector<RawVertexFormat>& vertices)
{
    assert((indices.size() % 3) == 0);
    for (auto it = indices.begin(); it != indices.end(); it += 3)
    {
        std::swap(*it, *(it + 2));
    }

    for (RawVertexFormat& v : vertices)
    {
        v.m_TexCoord.x = ConvertFloatToHalf(1.0f - ConvertHalfToFloat(v.m_TexCoord.x));
    }
}

static void CreateUnitCubeMesh()
{
    PROFILE_FUNCTION();

    // A box has six faces, each one pointing in a different direction.
    const Vector3 faceNormals[] =
    {
        {  0,  0,  1 },
        {  0,  0, -1 },
        {  1,  0,  0 },
        { -1,  0,  0 },
        {  0,  1,  0 },
        {  0, -1,  0 },
    };

    const Vector4 faceTangents[] = 
    {
        { 1, 0, 0, 1 },
        { 1, 0, 0, 1 },
        { 0, 0, 1, 1 },
        { 0, 0, -1, 1 },
        { 1, 0, 0, 1 },
        { 1, 0, 0, 1 },
    };

    const uint16_t kHalfPrecision1 = 0x3C00; // 1.0f

    const Half2 textureCoordinates[] =
    {
        { 1.0f, 0.0f },
        { 1.0f, 1.0f },
        { 0.0f, 1.0f },
        { 0.0f, 0.0f },
    };

    std::vector<RawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    // Create each face in turn.
    for (uint32_t i = 0; i < std::size(faceNormals); ++i)
    {
        // Get two vectors perpendicular both to the face normal and to each other.
        const Vector3 basis = (i >= 4) ? Vector3::UnitZ : Vector3::UnitY;

        const Vector3 side1 = faceNormals[i].Cross(basis);
        const Vector3 side2 = faceNormals[i].Cross(side1);

        // Six indices (two triangles) per face.
        const Graphic::IndexBufferFormat_t vbase = (Graphic::IndexBufferFormat_t)vertices.size();
        indices.push_back(vbase + 0);
        indices.push_back(vbase + 1);
        indices.push_back(vbase + 2);

        indices.push_back(vbase + 0);
        indices.push_back(vbase + 2);
        indices.push_back(vbase + 3);

        // Four vertices per face.
        // (faceNormals[i] - side1 - side2) * 0.5f // faceNormals[i] // t0
        vertices.push_back(RawVertexFormat{ (faceNormals[i] - side1 - side2) * 0.5f, faceNormals[i], faceTangents[i], textureCoordinates[0] });

        // (faceNormals[i] - side1 + side2) * 0.5f // faceNormals[i] // t1
        vertices.push_back(RawVertexFormat{ (faceNormals[i] - side1 + side2) * 0.5f, faceNormals[i], faceTangents[i], textureCoordinates[1] });

        // (faceNormals[i] + side1 + side2) * 0.5f // faceNormals[i] // t2
        vertices.push_back(RawVertexFormat{ (faceNormals[i] + side1 + side2) * 0.5f, faceNormals[i], faceTangents[i], textureCoordinates[2] });

        // (faceNormals[i] + side1 - side2) * 0.5f // faceNormals[i] // t3
        vertices.push_back(RawVertexFormat{ (faceNormals[i] + side1 - side2) * 0.5f, faceNormals[i], faceTangents[i], textureCoordinates[3] });
    }

    ReverseWinding(indices, vertices);

    g_Graphic.m_Meshes.emplace_back().Initialize(vertices, indices, "Default Unit Cube Mesh");
}

static void CreateUnitSphereMesh()
{
    PROFILE_FUNCTION();

    static const uint32_t tessellation = 12;

    std::vector<RawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    const uint32_t verticalSegments = tessellation;
    const uint32_t horizontalSegments = tessellation * 2;

    const float radius = 0.5f;

    // Create rings of vertices at progressively higher latitudes.
    for (uint32_t i = 0; i <= verticalSegments; i++)
    {
        const float v = 1 - float(i) / float(verticalSegments);

        float dy, dxz;
        const float latitude = (float(i) * std::numbers::pi / float(verticalSegments)) - (std::numbers::pi * 0.5f);
        ScalarSinCos(dy, dxz, latitude);

        // Create a single ring of vertices at this latitude.
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const float u = float(j) / float(horizontalSegments);

            float dx, dz;
            const float longitude = float(j) * (std::numbers::pi * 2) / float(horizontalSegments);
            ScalarSinCos(dx, dz, longitude);

            dx *= dxz;
            dz *= dxz;

            const Vector3 normal{ dx, dy, dz };
            const Vector4 tangent{ -dz, 0, dx, 1 };
            const Half2 textureCoordinate{ u, v };

            vertices.push_back(RawVertexFormat{ { normal* radius }, normal, tangent, textureCoordinate });
        }
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    const uint32_t stride = horizontalSegments + 1;

    for (uint32_t i = 0; i < verticalSegments; i++)
    {
        for (uint32_t j = 0; j <= horizontalSegments; j++)
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

    //if (invertn)
    //    InvertNormals(vertices);

    g_Graphic.m_Meshes.emplace_back().Initialize(vertices, indices, "Default Unit Sphere Mesh");
}

void CommonResources::Initialize()
{
    PROFILE_FUNCTION();

    CreateDefaultTexture("Black 2D Texture",       BlackTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture("White 2D Texture",       WhiteTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 1.0f, 1.0f }.RGBA().v, 1, false /*bUAV*/);
    CreateDefaultTexture("Dummy UAV 2D Texture",   DummyUAV2DTexture,  nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, true /*bUAV*/);
    CreateDefaultTexture("R8 UInt Max 2D Texture", R8UIntMax2DTexture, nvrhi::Format::R8_UINT, UINT8_MAX, 1, false /*bUAV*/);

    CreateDefaultBuffer("DummyUintStructuredBuffer", DummyUintStructuredBuffer, sizeof(uint32_t), sizeof(uint32_t), true);

    CreateUnitCubeMesh();
    CreateUnitSphereMesh();

    CreateDefaultSamplers();
    CreateDefaultInputLayouts();
    CreateDefaultDepthStencilStates();
}

#pragma warning(pop)
