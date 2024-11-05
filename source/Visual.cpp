#include "Visual.h"

#include "nvrhi/utils.h"

#include "DescriptorTableManager.h"
#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
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

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Texture::LoadFromMemory");

    extern bool IsSTBImage(const void* data, uint32_t nbBytes);
    if (IsSTBImage(rawData, nbBytes))
    {
        extern nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB = false);
        m_NVRHITextureHandle = CreateSTBITextureFromMemory(commandList, rawData, nbBytes, debugName.data());
    }
    else
    {
        extern nvrhi::TextureHandle CreateKTXTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName);
        m_NVRHITextureHandle = CreateKTXTextureFromMemory(commandList, rawData, nbBytes, debugName.data());
    }

    assert(m_NVRHITextureHandle);

    m_DescriptorIndex = GetDescriptorIndexForTexture(m_NVRHITextureHandle);

    const nvrhi::TextureDesc& texDesc = m_NVRHITextureHandle->getDesc();
    LOG_DEBUG("Texture: [%s][%d x %d][%s]", texDesc.debugName.c_str(), texDesc.width, texDesc.height, nvrhi::utils::FormatToString(texDesc.format));
}

void Texture::LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc)
{
    PROFILE_FUNCTION();

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
        commandList->writeTexture(newTexture, arraySlice, 0, rawData, textureDesc.width * BytesPerPixel(textureDesc.format));
    }

    commandList->setPermanentTextureState(newTexture, textureDesc.isUAV ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
}

void Texture::LoadFromFile(std::string_view filePath)
{
    PROFILE_FUNCTION();

    // if loading is required, sanity check that texture handle & descriptor table index is invalid
    assert(!IsValid());

    std::string extStr = GetFileExtensionFromPath(filePath);
    StringUtils::ToLower(extStr);

    std::vector<std::byte> imageBytes;
    ReadDataFromFile(filePath, imageBytes);

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

    m_NbVertices = (uint32_t)vertices.size();
    m_NbIndices = (uint32_t)indices.size();

    // init BV(s)
    Sphere::CreateFromPoints(m_BoundingSphere, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));;
    AABB::CreateFromPoints(m_AABB, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));

    // append into virtual buffers
    uint64_t byteOffset = g_Graphic.m_VirtualVertexBuffer.QueueAppend(vertices.data(), vertices.size() * sizeof(RawVertexFormat));
    m_StartVertexLocation = (uint32_t)byteOffset / sizeof(RawVertexFormat);

    byteOffset = g_Graphic.m_VirtualIndexBuffer.QueueAppend(indices.data(), indices.size() * sizeof(Graphic::IndexBufferFormat_t));
    m_StartIndexLocation = (uint32_t)byteOffset / sizeof(Graphic::IndexBufferFormat_t);

    MeshData meshData{};
    meshData.m_IndexCount = m_NbIndices;
    meshData.m_StartIndexLocation = m_StartIndexLocation;
    meshData.m_StartVertexLocation = m_StartVertexLocation;
    meshData.m_HasTangentData = vertices[0].m_Tangent.LengthSquared() > 0.0f; // huge assumption here!
    meshData.m_BoundingSphere = Vector4{ m_BoundingSphere.Center.x, m_BoundingSphere.Center.y, m_BoundingSphere.Center.z, m_BoundingSphere.Radius };
    meshData.m_AABBCenter = m_AABB.Center;
    meshData.m_AABBExtents = m_AABB.Extents;

    // NOTE: should not need to cache/retrieve the MeshData as the Mesh itself is cached
    byteOffset = g_Graphic.m_VirtualMeshDataBuffer.QueueAppend(&meshData, sizeof(MeshData));
    m_MeshDataBufferIdx = (uint32_t)byteOffset / sizeof(MeshData);

    LOG_DEBUG("Mesh: [%s][V: %d][I: %d]", meshName.data(), m_NbVertices, m_NbIndices);
}

bool Mesh::IsValid() const
{
    return m_NbVertices > 0
        && m_NbIndices > 0
        && m_StartVertexLocation != UINT_MAX
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
