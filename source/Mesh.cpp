#include "Mesh.h"

#include "Engine.h"
#include "Graphic.h"
#include "Utilities.h"

#include "shaders/shared/MeshData.h"

void Mesh::Initialize(std::span<const RawVertexFormat> vertices, std::span<const uint32_t> indices, std::string_view meshName)
{
    PROFILE_FUNCTION();

    m_Hash = HashVertices(vertices);
    m_NbVertices = (uint32_t)vertices.size();
    m_NbIndices = (uint32_t)indices.size();

    // init BV(s)
    Sphere::CreateFromPoints(m_BoundingSphere, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));
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
    meshData.m_BoundingSphere = Vector4{ m_BoundingSphere.Center.x, m_BoundingSphere.Center.y, m_BoundingSphere.Center.z, m_BoundingSphere.Radius };
    meshData.m_AABBCenter = m_AABB.Center;
    meshData.m_AABBExtents = m_AABB.Extents;

    // NOTE: should not need to cache/retrieve the MeshData as the Mesh itself is cached
    byteOffset = g_Graphic.m_VirtualMeshDataBuffer.QueueAppend(&meshData, sizeof(MeshData));
    m_MeshDataBufferIdx = (uint32_t)byteOffset / sizeof(MeshData);

    LOG_TO_CONSOLE("Mesh: [%s][V: %d][I: %d]", meshName.data(), m_NbVertices, m_NbIndices);
}

std::size_t Mesh::HashVertices(std::span<const RawVertexFormat> vertices)
{
    PROFILE_FUNCTION();

    std::size_t hash = 0;
    for (const RawVertexFormat& v : vertices)
    {
        HashCombine(hash, HashRawMem(v));
    }
    return hash;
}

bool Mesh::IsValid() const
{
    return m_Hash != 0 &&
        m_NbVertices > 0 &&
        m_NbIndices > 0 &&
        m_StartVertexLocation != UINT_MAX &&
        m_StartIndexLocation != UINT_MAX &&
        m_MeshDataBufferIdx != UINT_MAX;
}
