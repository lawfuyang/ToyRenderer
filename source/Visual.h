#pragma once

#include "nvrhi/nvrhi.h"

#include "MathUtilities.h"
#include "OctTree.h"

class Mesh;

// TODO: throw to its own file if it gets too big
class Texture
{
public:
    void LoadFromMemory(const void* rawData, uint32_t nbBytes, bool bIsKTX2, std::string_view debugName);
    void LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc);
    bool LoadFromFile(std::string_view filePath);
    bool LoadFromCache(bool bInsertEmptyTextureHandleIfNotFound = false, bool* bInserted = nullptr);

    operator bool() const { return m_NVRHITextureHandle != nullptr && m_DescriptorIndex != UINT_MAX; }

    std::size_t m_Hash = 0;
    uint32_t m_DescriptorIndex = UINT_MAX;
    nvrhi::TextureHandle m_NVRHITextureHandle;
    nvrhi::SamplerAddressMode m_AddressMode = nvrhi::SamplerAddressMode::Wrap;
};

// TODO: throw to its own file if it gets too big
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

    bool m_EnableAlphaBlend = false;

    uint32_t m_MaterialDataBufferIdx = UINT_MAX;
};

// TODO: throw to its own file if it gets too big
class Primitive
{
public:
    bool IsValid() const;

    uint32_t m_VisualIdx = UINT_MAX;
	uint32_t m_MeshIdx = UINT_MAX;
    uint32_t m_SceneOctTreeNodeIdx = UINT_MAX;
    Material m_Material;
    uint32_t m_ScenePrimitiveIndex = UINT_MAX;
};

class Visual
{
public:
    void UpdateIMGUI();
    void OnSceneLoad();

    void InsertPrimitivesToScene();
    void UpdatePrimitivesInScene();

    std::string m_Name = "Un-named Visual";
    uint32_t m_NodeID = UINT_MAX;
    std::vector<Primitive> m_Primitives;
};

class Node
{
public:
    void UpdateIMGUI();

    Matrix MakeLocalToWorldMatrix() const;

    uint32_t m_ID = UINT_MAX;

    Vector3 m_Position;
    Vector3 m_Scale = Vector3::One;
    //Vector3 m_Rotation; // yaw pitch roll
    Quaternion m_Rotation;

    AABB m_AABB = { Vector3::Zero, Vector3::Zero };
    Sphere m_BoundingSphere = { Vector3::Zero, 0.0f };

    std::string m_Name = "Un-named Node";

    uint32_t m_VisualIdx = UINT_MAX;

	uint32_t m_ParentNodeID = UINT_MAX;
    std::vector<uint32_t> m_ChildrenNodeIDs;
};
