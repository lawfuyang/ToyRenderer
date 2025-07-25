#pragma once

#include "extern/nvrhi/include/nvrhi/nvrhi.h"

#include "GraphicConstants.h"
#include "MathUtilities.h"
#include "DescriptorTableManager.h"

// NOTE: keep the values in sync with cgltf_alpha_mode
enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

struct TextureFileHeader
{
    uint32_t m_FileSize;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_MipCount;
    nvrhi::Format m_Format;
    uint32_t m_DXGIFormat; // DXGI_FORMAT enum value
    uint32_t m_ImageDataByteOffset;
};

struct TextureMipData
{
    Vector2U m_Resolution = { 0, 0 };
    uint32_t m_DataOffset = 0;
    uint32_t m_NumBytes = 0;
    uint32_t m_RowPitch = 0;
    std::vector<std::byte> m_Data;

    bool IsValid() const { return m_Resolution.x > 0 && m_Resolution.y > 0 && m_NumBytes > 0; }
};

struct FeedbackTextureTileInfo
{
    uint32_t m_Mip;
    uint32_t m_XInTexels;
    uint32_t m_YInTexels;
    uint32_t m_WidthInTexels;
    uint32_t m_HeightInTexels;
};

class Texture
{
public:
    void LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc);
    void LoadFromFile(std::string_view filePath);

    bool IsValid() const;

    bool IsTilePacked(uint32_t tileIdx) const;
    void GetTileInfo(uint32_t tileIndex, std::vector<FeedbackTextureTileInfo>& tiles) const;

    std::string m_ImageFilePath;
    TextureFileHeader m_TextureFileHeader;
    TextureMipData m_TextureMipDatas[GraphicConstants::kMaxTextureMips];
    
    uint32_t m_NumTiles;
    nvrhi::PackedMipDesc m_PackedMipDesc;
    nvrhi::TileShape m_TileShape;
    nvrhi::SubresourceTiling m_TilingsInfo[GraphicConstants::kMaxTextureMips];

    nvrhi::TextureHandle m_NVRHITextureHandle;
    uint32_t m_SRVIndexInTable = UINT_MAX;

    uint32_t m_TiledTextureID = UINT_MAX;
    nvrhi::SamplerFeedbackTextureHandle m_SamplerFeedbackTextureHandle;
    nvrhi::BufferHandle m_FeedbackResolveBuffers[2];
    nvrhi::TextureHandle m_MinMipTextureHandle;

    uint32_t m_SamplerFeedbackIndexInTable = UINT_MAX;
    uint32_t m_MinMipIndexInTable = UINT_MAX;
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
    struct TextureView
    {
        uint32_t m_TextureIdx = UINT_MAX; // Index in the global texture array
        nvrhi::SamplerAddressMode m_AddressMode = nvrhi::SamplerAddressMode::Wrap;

        bool IsValid() const { return m_TextureIdx != UINT_MAX; }
    };

    bool IsValid() const;

    uint32_t m_MaterialFlags = 0;

    TextureView m_Albedo;
    TextureView m_Normal;
    TextureView m_MetallicRoughness;
    TextureView m_Emissive;

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
