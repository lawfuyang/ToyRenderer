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
    
    static const nvrhi::VertexAttributeDesc s_RawVertexFormatLayout[] =
    {
        { "POSITION"    , nvrhi::Format::RGB32_FLOAT, 1, 0, offsetof(RawVertexFormat, m_Position)   , sizeof(RawVertexFormat), false },
        { "NORMAL"      , nvrhi::Format::RGB32_FLOAT, 1, 0, offsetof(RawVertexFormat, m_Normal)     , sizeof(RawVertexFormat), false },
        { "TEXCOORD"    , nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(RawVertexFormat, m_TexCoord)   , sizeof(RawVertexFormat), false }
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
    g_CommonResources.RawVertexLayout = device->createInputLayout(s_RawVertexFormatLayout, (uint32_t)std::size(s_RawVertexFormatLayout), dummyVS);
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
        v.m_TexCoord.x = (1.0f - v.m_TexCoord.x);
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

    const Vector2 textureCoordinates[] =
    {
        { 1, 0 },
        { 1, 1 },
        { 0, 1 },
        { 0, 0 },
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
        vertices.push_back(RawVertexFormat{ (faceNormals[i] - side1 - side2) * 0.5f, faceNormals[i], textureCoordinates[0] });

        // (faceNormals[i] - side1 + side2) * 0.5f // faceNormals[i] // t1
        vertices.push_back(RawVertexFormat{ (faceNormals[i] - side1 + side2) * 0.5f, faceNormals[i], textureCoordinates[1] });

        // (faceNormals[i] + side1 + side2) * 0.5f // faceNormals[i] // t2
        vertices.push_back(RawVertexFormat{ (faceNormals[i] + side1 + side2) * 0.5f, faceNormals[i], textureCoordinates[2] });

        // (faceNormals[i] + side1 - side2) * 0.5f // faceNormals[i] // t3
        vertices.push_back(RawVertexFormat{ (faceNormals[i] + side1 - side2) * 0.5f, faceNormals[i], textureCoordinates[3] });
    }

    ReverseWinding(indices, vertices);

    bool bRetrievedFromCache = false;
    g_CommonResources.UnitCube.m_Mesh = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);
    assert(!bRetrievedFromCache);
    g_CommonResources.UnitCube.m_Mesh->Initialize(vertices, indices, "Default Unit Cube Mesh");

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
        const float latitude = (float(i) * PI / float(verticalSegments)) - PIBy2;
        ScalarSinCos(dy, dxz, latitude);

        // Create a single ring of vertices at this latitude.
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const float u = float(j) / float(horizontalSegments);

            float dx, dz;
            const float longitude = float(j) * (PI * 2) / float(horizontalSegments);
            ScalarSinCos(dx, dz, longitude);

            dx *= dxz;
            dz *= dxz;

            const Vector3 normal{ dx, dy, dz };
            const Vector2 textureCoordinate{ u, v };

            vertices.push_back(RawVertexFormat{ { normal* radius }, normal, textureCoordinate });
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

    bool bRetrievedFromCache = false;
    g_CommonResources.UnitSphere.m_Mesh = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);
    assert(!bRetrievedFromCache);
    g_CommonResources.UnitSphere.m_Mesh->Initialize(vertices, indices, "Default UnitSphere Mesh");
}

// Helper computes a point on a unit circle, aligned to the x/z plane and centered on the origin.
static Vector3 GetCircleVector(uint32_t i, uint32_t tessellation)
{
    const float angle = float(i) * (PI * 2) / float(tessellation);
    float dx, dz;

    ScalarSinCos(dx, dz, angle);

    return Vector3{ dx, 0, dz };
}

static Vector3 GetCircleTangent(uint32_t i, uint32_t tessellation)
{
    const float angle = (float(i) * (PI * 2) / float(tessellation)) + PIBy2;
    float dx, dz;

    ScalarSinCos(dx, dz, angle);

    return Vector3{ dx, 0, dz };
}

// Helper creates a triangle fan to close the end of a cylinder / cone
static void CreateCylinderCap(std::vector<RawVertexFormat>& vertices, std::vector<Graphic::IndexBufferFormat_t>& indices, uint32_t tessellation, float height, float radius, bool isTop)
{
    // Create cap indices.
    for (uint32_t i = 0; i < tessellation - 2; i++)
    {
        uint32_t i1 = (i + 1) % tessellation;
        uint32_t i2 = (i + 2) % tessellation;

        if (isTop)
        {
            std::swap(i1, i2);
        }

        const uint32_t vbase = vertices.size();
        indices.push_back(vbase);
        indices.push_back(vbase + i1);
        indices.push_back(vbase + i2);
    }

    // Which end of the cylinder is this?
    Vector3 normal = Vector3::UnitY;
    Vector2 textureScale = -0.5f * Vector2::One;

    if (!isTop)
    {
        normal *= -1.0f;
        textureScale.x *= -1.0f;
    }

    // Create cap vertices.
    for (uint32_t i = 0; i < tessellation; i++)
    {
        const Vector3 circleVector = GetCircleVector(i, tessellation);

        const Vector3 position = (circleVector * radius) + (normal * height);

        const Vector2 textureCoordinate = Vector2{ circleVector.x, circleVector.z } *textureScale + (0.5f * Vector2::One);

        vertices.push_back({ position, normal, textureCoordinate });
    }
}

static void CreateCylinderMesh()
{
    PROFILE_FUNCTION();

    const float height = 0.5f;
    const float diameter = 1.0f;
    const uint32_t tessellation = 32;
    const Vector3 topOffset = Vector3::UnitY * height;
    const float radius = diameter / 2;
    const uint32_t stride = tessellation + 1;

    std::vector<RawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    // Create a ring of triangles around the outside of the cylinder.
    for (uint32_t i = 0; i <= tessellation; i++)
    {
        const Vector3 normal = GetCircleVector(i, tessellation);

        const Vector3 sideOffset = normal * radius;

        const float u = float(i) / float(tessellation);

        const Vector2 textureCoordinate{ u, 0.0f };

        vertices.push_back({ (sideOffset + topOffset), normal, textureCoordinate });
        vertices.push_back({ (sideOffset - topOffset), normal, textureCoordinate + Vector2::UnitY });

        indices.push_back(i * 2);
        indices.push_back((i * 2 + 2) % (stride * 2));
        indices.push_back(i * 2 + 1);

        indices.push_back(i * 2 + 1);
        indices.push_back((i * 2 + 2) % (stride * 2));
        indices.push_back((i * 2 + 3) % (stride * 2));
    }

    // Create flat triangle fan caps to seal the top and bottom.
    CreateCylinderCap(vertices, indices, tessellation, height, radius, true);
    CreateCylinderCap(vertices, indices, tessellation, height, radius, false);

    // Build RH above
    ReverseWinding(indices, vertices);

    bool bRetrievedFromCache = false;
    g_CommonResources.Cylinder.m_Mesh = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);
    assert(!bRetrievedFromCache);
    g_CommonResources.Cylinder.m_Mesh->Initialize(vertices, indices, "Default Cylinder Mesh");
}

static void CreateConeMesh()
{
    PROFILE_FUNCTION();

    const float diameter = 1.0f;
    const float height = 0.5f;
    const uint32_t tessellation = 32;
    const Vector3 topOffset = Vector3::UnitY * height;
    const float radius = diameter / 2.0f;
    const uint32_t stride = tessellation + 1;

    std::vector<RawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    // Create a ring of triangles around the outside of the cone.
    for (uint32_t i = 0; i <= tessellation; i++)
    {
        const Vector3 circlevec = GetCircleVector(i, tessellation);

        const Vector3 sideOffset = XMVectorScale(circlevec, radius);

        const float u = float(i) / float(tessellation);

        const Vector2 textureCoordinate{ u, 0.0f };

        const Vector3 pt = XMVectorSubtract(sideOffset, topOffset);

        Vector3 normal = XMVector3Cross(
            GetCircleTangent(i, tessellation),
            XMVectorSubtract(topOffset, pt));
        normal = XMVector3Normalize(normal);

        // Duplicate the top vertex for distinct normals
        vertices.push_back({ topOffset, normal, Vector2::Zero });
        vertices.push_back({ pt, normal, textureCoordinate + Vector3::UnitY });

        indices.push_back(i * 2);
        indices.push_back((i * 2 + 3) % (stride * 2));
        indices.push_back((i * 2 + 1) % (stride * 2));
    }

    // Create flat triangle fan caps to seal the bottom.
    CreateCylinderCap(vertices, indices, tessellation, height, radius, false);

    // Build RH above
    ReverseWinding(indices, vertices);

    bool bRetrievedFromCache = false;
    g_CommonResources.Cone.m_Mesh = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);
    assert(!bRetrievedFromCache);
    g_CommonResources.Cone.m_Mesh->Initialize(vertices, indices, "Default Cone Mesh");
}

static void CreateTorusMesh()
{
    PROFILE_FUNCTION();

    const float diameter = 1.0f;
    const float thickness = 0.333f;
    const uint32_t tessellation = 32;
    const uint32_t stride = tessellation + 1;

    std::vector<RawVertexFormat> vertices;
    std::vector<Graphic::IndexBufferFormat_t> indices;

    // First we loop around the main ring of the torus.
    for (uint32_t i = 0; i <= tessellation; i++)
    {
        const float u = float(i) / float(tessellation);

        const float outerAngle = float(i) * (PI * 2) / float(tessellation) - PIBy2;

        // Create a transform matrix that will align geometry to slice perpendicularly though the current ring position.
        const Matrix transform = Matrix::CreateTranslation(diameter / 2, 0, 0) * Matrix::CreateRotationY(outerAngle);

        // Now we loop along the other axis, around the side of the tube.
        for (uint32_t j = 0; j <= tessellation; j++)
        {
            const float v = 1 - float(j) / float(tessellation);

            const float innerAngle = float(j) * (PI * 2) / float(tessellation) + PI;
            float dx, dy;

            ScalarSinCos(dy, dx, innerAngle);

            // Create a vertex.
            Vector3 normal{ dx, dy, 0 };
            Vector3 position = normal * thickness / 2;

            position = Vector3::Transform(position, transform);
            normal = Vector3::TransformNormal(normal, transform);

            vertices.push_back({ position, normal, { u, v } });

            // And create indices for two triangles.
            const uint32_t nextI = (i + 1) % stride;
            const uint32_t nextJ = (j + 1) % stride;

            indices.push_back(i * stride + j);
            indices.push_back(i * stride + nextJ);
            indices.push_back(nextI * stride + j);

            indices.push_back(i * stride + nextJ);
            indices.push_back(nextI * stride + nextJ);
            indices.push_back(nextI * stride + j);
        }
    }

    // Build RH above
    ReverseWinding(indices, vertices);

    bool bRetrievedFromCache = false;
    g_CommonResources.Torus.m_Mesh = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);
    assert(!bRetrievedFromCache);
    g_CommonResources.Torus.m_Mesh->Initialize(vertices, indices, "Default Torus Mesh");
}

static void CreateDefaultMaterial()
{
    MaterialData materialData{};
    materialData.m_ConstDiffuse = Vector3{ 1.0f, 0.078f, 0.576f };
    materialData.m_ConstRoughness = 1.0f;
    materialData.m_ConstMetallic = 0.0f;

    g_CommonResources.DefaultMaterial.m_MaterialFlags = materialData.m_MaterialFlags;
    g_CommonResources.DefaultMaterial.m_MaterialDataBufferIdx = g_Graphic.AppendOrRetrieveMaterialDataIndex(materialData);
}

void CommonResources::Initialize()
{
    PROFILE_FUNCTION();

    tf::Taskflow tf;

    tf.emplace([this] { CreateDefaultTexture("Black 2D Texture",       BlackTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Black 2D Array Texture", BlackArrayTexture,  nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 4, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Gray 2D Texture",        GrayTexture,        nvrhi::Format::RGBA8_UNORM, Color{ 0.5f, 0.5f, 0.5f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("White 2D Texture",       WhiteTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 1.0f, 1.0f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("White 2D Aray Texture",  WhiteArrayTexture,  nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 1.0f, 1.0f }.RGBA().v, 4, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Red 2D Texture",         RedTexture,         nvrhi::Format::RGBA8_UNORM, Color{ 1.0f, 0.0f, 0.0f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Green 2D Texture",       GreenTexture,       nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 1.0f, 0.0f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Blue 2D Texture",        BlueTexture,        nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 1.0f }.RGBA().v, 1, false/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("Dummy UAV 2D Texture",   DummyUAV2DTexture,  nvrhi::Format::RGBA8_UNORM, Color{ 0.0f, 0.0f, 0.0f }.RGBA().v, 1, true/*bUAV*/); });
    tf.emplace([this] { CreateDefaultTexture("R8 UInt Max 2D Texture", R8UIntMax2DTexture, nvrhi::Format::R8_UINT, UINT8_MAX, 1, false/*bUAV*/); });

    tf.emplace([this] { CreateDefaultBuffer("DummyUintStructuredBuffer", DummyUintStructuredBuffer, sizeof(uint32_t), sizeof(uint32_t), true); });

    tf.emplace([] { CreateUnitCubeMesh(); });
    tf.emplace([] { CreateUnitSphereMesh(); });
    tf.emplace([] { CreateCylinderMesh(); });
    tf.emplace([] { CreateConeMesh(); });
    tf.emplace([] { CreateTorusMesh(); });

    tf.emplace([] { CreateDefaultSamplers(); });
    tf.emplace([] { CreateDefaultInputLayouts(); });
    tf.emplace([] { CreateDefaultDepthStencilStates(); });

    g_Engine.m_Executor->corun(tf);

    CreateDefaultMaterial();
}

#pragma warning(pop)
