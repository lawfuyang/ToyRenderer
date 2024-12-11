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

void Mesh::Initialize(std::span<const RawVertexFormat> vertices, std::span<const uint32_t> indices, std::string_view meshName)
{
    PROFILE_FUNCTION();

    // init BV(s)
    Sphere::CreateFromPoints(m_BoundingSphere, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));
    AABB::CreateFromPoints(m_AABB, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));

    std::vector<meshopt_Meshlet> meshlets;
    std::vector<unsigned int> meshletVertices;
    std::vector<unsigned char> meshletTriangles;

    const uint32_t numMaxMeshlets = meshopt_buildMeshletsBound(indices.size(), kMeshletMaxVertices, kMeshletMaxTriangles);
    meshlets.resize(numMaxMeshlets);
    meshletVertices.resize(numMaxMeshlets * kMeshletMaxVertices);
    meshletTriangles.resize(numMaxMeshlets * kMeshletMaxTriangles * 3);

    const float kMeshletConeWeight = 0.25f;
    const uint32_t numMeshlets = meshopt_buildMeshlets(
        meshlets.data(),
        meshletVertices.data(),
        meshletTriangles.data(),
        indices.data(),
        indices.size(),
        (const float*)vertices.data(),
        vertices.size(),
        sizeof(RawVertexFormat),
        kMeshletMaxVertices,
        kMeshletMaxTriangles,
        kMeshletConeWeight);

    for (const meshopt_Meshlet& meshlet : meshlets)
    {
        meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
    }

    LOG_DEBUG("New Mesh: %s, vertices: %d, indices: %d, numMeshlets: %d", meshName.data(), vertices.size(), indices.size(), numMeshlets);
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
