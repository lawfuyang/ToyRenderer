#pragma once

#include "MathUtilities.h"

#include "shaders/shared/RawVertexFormat.h"

class Mesh
{
public:
    void Initialize(std::span<const RawVertexFormat> rawVertices, std::span<const uint32_t> indices, std::string_view meshName);

    static std::size_t HashVertices(std::span<const RawVertexFormat> vertices);

    bool IsValid() const;

    std::size_t m_Hash = 0;

    uint32_t m_NbVertices = 0;
    uint32_t m_NbIndices = 0;

    uint32_t m_StartVertexLocation = UINT_MAX;
    uint32_t m_StartIndexLocation = UINT_MAX;

    uint32_t m_MeshDataBufferIdx = UINT_MAX;

    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };
};
