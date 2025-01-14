#include "Visual.h"

#include "extern/meshoptimizer/src/meshoptimizer.h"

#include "DescriptorTableManager.h"
#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "TextureLoading.h"
#include "Utilities.h"

#include "shaders/shared/CommonConsts.h"
#include "shaders/shared/MeshData.h"

static uint32_t GetDescriptorIndexForTexture(nvrhi::TextureHandle texture)
{
    return g_Graphic.m_DescriptorTableManager->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, texture));
}

void Texture::LoadFromMemory(const void* rawData, uint32_t nbBytes, std::string_view debugName)
{
    PROFILE_FUNCTION();

    assert(!IsValid());

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Texture::LoadFromMemory");

    if (IsSTBImage(rawData, nbBytes))
    {
        m_NVRHITextureHandle = CreateSTBITextureFromMemory(commandList, rawData, nbBytes, debugName.data());
    }
    else if (IsDDSImage(rawData))
    {
        m_NVRHITextureHandle = CreateDDSTextureFromMemory(commandList, rawData, nbBytes, debugName.data());
    }
    else if (IsKTX2Image(rawData, nbBytes))
    {
        m_NVRHITextureHandle = CreateKTXTextureFromMemory(commandList, rawData, nbBytes, debugName.data());
    }
    else
    {
        assert(0);
    }

    assert(m_NVRHITextureHandle);

    m_DescriptorIndex = GetDescriptorIndexForTexture(m_NVRHITextureHandle);

    const nvrhi::TextureDesc& texDesc = m_NVRHITextureHandle->getDesc();
    LOG_DEBUG("New Texture: %s, %d x %d, %s", texDesc.debugName.c_str(), texDesc.width, texDesc.height, nvrhi::utils::FormatToString(texDesc.format));
}

void Texture::LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc)
{
    PROFILE_FUNCTION();

    assert(!IsValid());

    // TODO: extend this function to accomodate the following asserts
    assert(textureDesc.depth == 1);

    nvrhi::TextureHandle newTexture = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);
    m_NVRHITextureHandle = newTexture;
    m_DescriptorIndex = GetDescriptorIndexForTexture(newTexture);

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Texture::LoadFromMemory");

    // fill texture data for mip0
    // NOTE: fills each array slice with the same exact src data bytes
    for (uint32_t arraySlice = 0; arraySlice < textureDesc.arraySize; arraySlice++)
    {
        commandList->writeTexture(newTexture, arraySlice, 0, rawData, textureDesc.width * nvrhi::getFormatInfo(textureDesc.format).bytesPerBlock);
    }

    commandList->setPermanentTextureState(newTexture, textureDesc.isUAV ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
}

void Texture::LoadFromFile(std::string_view filePath)
{
    PROFILE_FUNCTION();

    assert(!IsValid());

    std::vector<std::byte> imageBytes;
    ReadDataFromFile(filePath, imageBytes);
    assert(!imageBytes.empty());

    const std::string debugName = std::filesystem::path{ filePath }.stem().string();
    LoadFromMemory(imageBytes.data(), (uint32_t)imageBytes.size(), debugName);
}

bool Texture::IsValid() const
{
    return m_NVRHITextureHandle != nullptr
        && m_DescriptorIndex != UINT_MAX;
}

bool Primitive::IsValid() const
{
    return m_NodeID != UINT_MAX
        && m_MeshIdx != UINT_MAX
        && m_Material.IsValid();
}

void Mesh::Initialize(
    std::span<const RawVertexFormat> vertices,
    std::span<const uint32_t> indices,
    std::vector<uint32_t>& meshletVertexIdxOffsetsOut,
    std::vector<uint32_t>& meshletIndicesOut,
    std::vector<MeshletData>& meshletsOut,
    std::string_view meshName)
{
    PROFILE_FUNCTION();

    // init BV(s)
    Sphere::CreateFromPoints(m_BoundingSphere, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));
    AABB::CreateFromPoints(m_AABB, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));

    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletTriangles;

    const uint32_t numMaxMeshlets = meshopt_buildMeshletsBound(indices.size(), kMaxMeshletSize, kMaxMeshletSize);
    meshlets.resize(numMaxMeshlets);
    meshletVertices.resize(numMaxMeshlets * kMaxMeshletSize);
    meshletTriangles.resize(numMaxMeshlets * kMaxMeshletSize * 3);

    const float kMeshletConeWeight = 0.25f;
    m_NumMeshlets = meshopt_buildMeshlets(
        meshlets.data(),
        meshletVertices.data(),
        meshletTriangles.data(),
        indices.data(),
        indices.size(),
        (const float*)vertices.data(),
        vertices.size(),
        sizeof(RawVertexFormat),
        kMaxMeshletSize,
        kMaxMeshletSize,
        kMeshletConeWeight);

	meshlets.resize(m_NumMeshlets);

    for (const meshopt_Meshlet& meshlet : meshlets)
    {
        meshopt_optimizeMeshlet(&meshletVertices.at(meshlet.vertex_offset), &meshletTriangles.at(meshlet.triangle_offset), meshlet.triangle_count, meshlet.vertex_count);

        MeshletData& newMeshlet = meshletsOut.emplace_back();
        newMeshlet.m_VertexBufferIdx = meshletVertexIdxOffsetsOut.size();
        newMeshlet.m_IndicesBufferIdx = meshletIndicesOut.size();

        for (uint32_t i = 0; i < meshlet.vertex_count; ++i)
        {
            meshletVertexIdxOffsetsOut.push_back(meshletVertices.at(meshlet.vertex_offset + i));
        }

        for (uint32_t i = 0; i < meshlet.triangle_count; ++i)
        {
            const uint32_t baseOffset = meshlet.triangle_offset + (i * 3);
            const uint8_t a = meshletTriangles.at(baseOffset + 0);
            const uint8_t b = meshletTriangles.at(baseOffset + 1);
            const uint8_t c = meshletTriangles.at(baseOffset + 2);

            const uint32_t packedIndices = a | (b << 8) | (c << 16);

            meshletIndicesOut.push_back(packedIndices);
        }

        const meshopt_Bounds meshletBounds = meshopt_computeMeshletBounds(&meshletVertices.at(meshlet.vertex_offset), &meshletTriangles.at(meshlet.triangle_offset), meshlet.triangle_count, (const float*)vertices.data(), vertices.size(), sizeof(RawVertexFormat));

        assert(meshlet.vertex_count <= UINT8_MAX);
        assert(meshlet.triangle_count <= UINT8_MAX);
        assert(Vector3{ meshletBounds.cone_axis }.Length() < (1.0f + kKindaSmallNumber));
        assert(meshletBounds.cone_cutoff_s8 <= (UINT8_MAX / 2));

		newMeshlet.m_VertexAndTriangleCount = meshlet.vertex_count | (meshlet.triangle_count << 8);
		newMeshlet.m_BoundingSphere = Vector4{ meshletBounds.center[0], meshletBounds.center[1], meshletBounds.center[2], meshletBounds.radius };

		const uint32_t packedAxisX = (meshletBounds.cone_axis[0] + 1.0f) * 0.5f * UINT8_MAX;
		const uint32_t packedAxisY = (meshletBounds.cone_axis[1] + 1.0f) * 0.5f * UINT8_MAX;
		const uint32_t packedAxisZ = (meshletBounds.cone_axis[2] + 1.0f) * 0.5f * UINT8_MAX;
        const uint32_t packedCutoff = meshletBounds.cone_cutoff_s8 * 2;

        assert(packedAxisX <= UINT8_MAX);
        assert(packedAxisY <= UINT8_MAX);
        assert(packedAxisZ <= UINT8_MAX);
        assert(packedCutoff <= UINT8_MAX);

		newMeshlet.m_ConeAxisAndCutoff = packedAxisX | (packedAxisY << 8) | (packedAxisZ << 16) | (packedCutoff << 24);

        // NOTE: m_VertexBufferIdx & m_IndicesBufferIdx will be properly offset to the global value after all mesh data are loaded
    }

    LOG_DEBUG("New Mesh: %s, vertices: %d, indices: %d, numMeshlets: %d", meshName.data(), vertices.size(), indices.size(), m_NumMeshlets);
}

bool Mesh::IsValid() const
{
    return m_StartVertexLocation != UINT_MAX
        && m_StartIndexLocation != UINT_MAX
        && m_MeshDataBufferIdx != UINT_MAX;
}

bool Material::IsValid() const
{
    bool bResult = true;

    if (m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        bResult &= m_AlbedoTexture.IsValid();
    }
    if (m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        bResult &= m_NormalTexture.IsValid();
    }
    if (m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        bResult &= m_MetallicRoughnessTexture.IsValid();
    }

    bResult &= m_MaterialDataBufferIdx != UINT_MAX;

    return bResult;
}

Matrix Node::MakeLocalToWorldMatrix() const
{
    const Matrix translateMat = Matrix::CreateTranslation(m_Position);
    //const Matrix rotationMat = Matrix::CreateFromYawPitchRoll(m_Rotation.x, m_Rotation.y, m_Rotation.z);
    const Matrix rotationMat = Matrix::CreateFromQuaternion(m_Rotation);
    const Matrix scaleMat = Matrix::CreateScale(m_Scale);

    Matrix worldMatrix = rotationMat * scaleMat * translateMat;

    if (m_ParentNodeID != UINT_MAX)
    {
        worldMatrix *= g_Graphic.m_Scene->m_Nodes.at(m_ParentNodeID).MakeLocalToWorldMatrix();
    }

    return worldMatrix;
}
