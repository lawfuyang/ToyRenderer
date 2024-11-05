#pragma once

#include "nvrhi/nvrhi.h"

#include "MathUtilities.h"

#include "shaders/shared/RawVertexFormat.h"

class Texture
{
public:
    void LoadFromMemory(const void* rawData, uint32_t nbBytes, std::string_view debugName);
    void LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc);
    void LoadFromFile(std::string_view filePath);

    bool IsValid() const;

    uint32_t m_DescriptorIndex = UINT_MAX;
    nvrhi::TextureHandle m_NVRHITextureHandle;
    nvrhi::SamplerAddressMode m_AddressMode = nvrhi::SamplerAddressMode::Wrap;
    Vector2 m_UVOffset;
    Vector2 m_UVScale = Vector2::One;
};

class Mesh
{
public:
    void Initialize(std::span<const RawVertexFormat> rawVertices, std::span<const uint32_t> indices, std::string_view meshName);

    bool IsValid() const;

    uint32_t m_Idx = UINT_MAX;

    uint32_t m_NbVertices = 0;
    uint32_t m_NbIndices = 0;

    uint32_t m_StartVertexLocation = UINT_MAX;
    uint32_t m_StartIndexLocation = UINT_MAX;

    uint32_t m_MeshDataBufferIdx = UINT_MAX;

    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };
};

class Material
{
public:
    bool IsValid() const;

    uint32_t m_MaterialFlags = 0;

    Texture m_AlbedoTexture;
    Texture m_NormalTexture;
    Texture m_MetallicRoughnessTexture;

    Vector3 m_ConstDiffuse = Vector3{ 1.0f, 0.0f, 0.0f };
    float m_ConstRoughness = 0.75f;
    float m_ConstMetallic = 0.1f;

    uint32_t m_MaterialDataBufferIdx = UINT_MAX;
};

class Primitive
{
public:
    bool IsValid() const;

    uint32_t m_NodeID = UINT_MAX;
	uint32_t m_MeshIdx = UINT_MAX;
    Material m_Material;
};

class Node
{
public:
    Matrix MakeLocalToWorldMatrix() const;

    Vector3 m_Position;
    Vector3 m_Scale = Vector3::One;
    Quaternion m_Rotation;

    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };
    
    std::vector<uint32_t> m_PrimitivesIDs;

	uint32_t m_ParentNodeID = UINT_MAX;
    std::vector<uint32_t> m_ChildrenNodeIDs;
};
