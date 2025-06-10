#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "MathUtilities.h"

// NOTE: keep the values in sync with cgltf_alpha_mode
enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

struct StreamingMipData
{
    Vector2U m_Resolution = { 0, 0 };
    uint32_t m_DataOffset = 0;
    uint32_t m_NumBytes = 0;
    uint32_t m_RowPitch = 0;

    bool IsValid() const { return m_Resolution.x > 0 && m_Resolution.y > 0 && m_NumBytes > 0; }
};

class Texture
{
public:
    void LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc);
    void LoadFromFile(std::string_view filePath);

    bool IsValid() const;

    std::string m_StreamingFilePath;
    StreamingMipData m_StreamingMipDatas[14];
    uint32_t m_HighestStreamedMip = UINT_MAX;

    uint32_t m_DescriptorIndex = UINT_MAX;
    nvrhi::TextureHandle m_NVRHITextureHandle;
    nvrhi::SamplerAddressMode m_AddressMode = nvrhi::SamplerAddressMode::Wrap;
};

struct MeshLOD
{
    uint32_t m_NumIndices = 0;
    uint32_t m_MeshletDataBufferIdx = UINT_MAX;
    uint32_t m_NumMeshlets = 0;
    float m_Error = 0.0f;
};

class Mesh
{
public:
    static uint32_t PackNormal(const Vector3& normal);

    void Initialize(
        const std::vector<struct RawVertexFormat>& rawVertices,
        const std::vector<uint32_t>& indices,
        uint32_t globalVertexBufferIdx,
        uint32_t globalIndexBufferIdxOffset,
        std::vector<uint32_t>& meshletVertexIdxOffsetsOut,
        std::vector<uint32_t>& meshletIndicesOut,
		std::vector<struct MeshletData>& meshletsOut,
        std::string_view meshName);

    void BuildBLAS(nvrhi::CommandListHandle commandList);

    bool IsValid() const;

    uint64_t m_GlobalIndexBufferIdx = 0;
    uint64_t m_GlobalVertexBufferIdx = 0;
    uint32_t m_NumIndices = 0;
    uint32_t m_NumVertices = 0;

    MeshLOD m_LODs[8];
    uint32_t m_NumLODs = 0;
    uint32_t m_MeshDataBufferIdx = UINT_MAX;
    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };
    nvrhi::rt::AccelStructHandle m_BLAS; // TODO: move to per-LOD BLAS
    std::string m_DebugName;
};

class Material
{
public:
    bool IsValid() const;

    uint32_t m_MaterialFlags = 0;

    uint32_t m_AlbedoTextureIdx = UINT_MAX;
    uint32_t m_NormalTextureIdx = UINT_MAX;
    uint32_t m_MetallicRoughnessTextureIdx = UINT_MAX;
    uint32_t m_EmissiveTextureIdx = UINT_MAX;

    Vector4 m_ConstAlbedo = Vector4{ 1.0f, 0.078f, 0.576f, 1.0f };
    Vector3 m_ConstEmissive;
    float m_ConstRoughness = 1.0f;
    float m_ConstMetallic = 0.0f;

    AlphaMode m_AlphaMode = AlphaMode::Opaque;
    float m_AlphaCutoff = 1.0f;

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

	uint32_t m_ParentNodeID = UINT_MAX;
    std::vector<uint32_t> m_ChildrenNodeIDs;
};
