#include "Visual.h"

#include "extern/debug_draw/debug_draw.hpp"
#include "extern/imgui/imgui.h"
#include "extern/imgui/imgui_stdlib.h"
#include "extern/portable-file-dialogs/portable-file-dialogs.h"
#include "nvrhi/utils.h"

#include "CommonResources.h"
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

void Texture::LoadFromMemory(const void* rawData, uint32_t nbBytes, bool bIsKTX2, std::string_view debugName)
{
    PROFILE_FUNCTION();

    // if hash is not already init, hash first 512 bytes of raw data. should be good enough
    if (m_Hash == 0)
    {
        m_Hash = HashRange((std::byte*)rawData, std::min(nbBytes, 512u));
    }

    const bool bRetrievedFromCache = LoadFromCache();
    if (bRetrievedFromCache)
    {
        assert(*this);
        return;
    }

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Texture::LoadFromMemory");

    const char* finalDebugName = debugName.empty() ? StringFormat("0x%X", m_Hash) : debugName.data();

    if (bIsKTX2)
    {
        extern nvrhi::TextureHandle CreateKTXTextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName);
        m_NVRHITextureHandle = CreateKTXTextureFromMemory(commandList, rawData, nbBytes, finalDebugName);
    }
    else
    {
        extern nvrhi::TextureHandle CreateSTBITextureFromMemory(nvrhi::CommandListHandle commandList, const void* data, uint32_t nbBytes, const char* debugName, bool forceSRGB = false);
        m_NVRHITextureHandle = CreateSTBITextureFromMemory(commandList, rawData, (uint32_t)nbBytes, finalDebugName);
    }
    assert(m_NVRHITextureHandle);

    {
        AUTO_LOCK(g_Graphic.m_TextureCacheLock);

        g_Graphic.m_TextureCache[m_Hash] = m_NVRHITextureHandle;
    }

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

bool Texture::LoadFromFile(std::string_view filePath)
{
    PROFILE_FUNCTION();

    // if load from file, hash file path
    m_Hash = std::hash<std::string_view>{}(filePath);

    // if the cache has a valid texture handle, means its fully loaded & ready to be retrieved
    // if dummy texture was inserted by this function call. it's now responsible to load it
    bool bDoLoad;
    const bool bRetrievedFromCache = LoadFromCache(true /*bInsertEmptyTextureHandleIfNotFound*/, &bDoLoad);

    if (bRetrievedFromCache)
    {
        // do nothing
        assert(*this);
        return true;

    }
    else if (bDoLoad)
    {
        // if loading is required, sanity check that texture handle & descriptor table index is invalid
        assert(!*this);

        std::string extStr = GetFileExtensionFromPath(filePath);
        StringUtils::ToLower(extStr);

        const bool bIsKTX2 = extStr == ".ktx2";

        std::vector<std::byte> imageBytes;
        ReadDataFromFile(filePath, imageBytes);

        const std::string debugName = std::filesystem::path{ filePath }.stem().string();
        LoadFromMemory(imageBytes.data(), (uint32_t)imageBytes.size(), bIsKTX2, debugName);
        return true;
    }

    // same texture file is currently being loaded in another thread...
    return false;
}

bool Texture::LoadFromCache(bool bInsertEmptyTextureHandleIfNotFound, bool* bOutInserted)
{
    assert(m_Hash != 0);

    if (bInsertEmptyTextureHandleIfNotFound)
    {
        AUTO_LOCK(g_Graphic.m_TextureCacheLock);
        auto [insertIt, bInserted] = g_Graphic.m_TextureCache.insert({ m_Hash, nvrhi::TextureHandle{} });

        if (bOutInserted)
        {
            *bOutInserted = bInserted;
        }
        else
        {
            m_NVRHITextureHandle = insertIt->second;
        }
    }
    else
    {
        AUTO_LOCK(g_Graphic.m_TextureCacheLock);
        auto it = g_Graphic.m_TextureCache.find(m_Hash);
        if (it == g_Graphic.m_TextureCache.end())
        {
            return false;
        }
        else
        {
            m_NVRHITextureHandle = it->second;
        }
    }

    // even if handle is cached, it could still be null as another thread might still be loading it
    if (m_NVRHITextureHandle)
    {
        m_DescriptorIndex = GetDescriptorIndexForTexture(m_NVRHITextureHandle);
        return true;
    }

    return false;
}

bool Primitive::IsValid() const
{
    return m_MeshIdx != UINT_MAX;
}

void Visual::UpdateIMGUI()
{
    for (uint32_t i = 0; i < m_Primitives.size(); ++i)
    {
        Primitive& primitive = m_Primitives[i];
        assert(primitive.IsValid());

        if (ImGui::CollapsingHeader(StringFormat("Primitive %d", i), ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto UpdateTextureIMGUI = [](Texture& texResource, std::string_view texName)
                {
                    if (ImGui::CollapsingHeader(texName.data(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        // preview texture
                        nvrhi::TextureHandle previewTex = !!texResource ? texResource.m_NVRHITextureHandle : g_CommonResources.BlackTexture.m_NVRHITextureHandle;
                        ImGui::Image(previewTex, ImVec2{ 64.0f, 64.0f });
                        ImGui::SameLine();

                        if (!!texResource)
                        {
                            ImGui::Text("%s", texResource.m_NVRHITextureHandle->getDesc().debugName.c_str());
                            ImGui::SameLine();
                        }

                        if (ImGui::Button("Browse..."))
                        {
                            std::vector<std::string> result = pfd::open_file(StringFormat("Select %s", texName.data()), GetResourceDirectory(), { "Image Files", "*.png *.jpg *.jpeg *.bmp" }, pfd::opt::force_path).result();
                            if (!result.empty())
                            {
                                const bool bResult = texResource.LoadFromFile(result[0]);
                                assert(bResult); // this should never return false
                            }
                        }
                        ImGui::SameLine();

                        // reset back to default texture
                        if (ImGui::Button("Reset"))
                        {
                            texResource.m_NVRHITextureHandle.Reset();
                        }
                    }
                };


            ImGui::Indent();

            Material& material = primitive.m_Material;
            UpdateTextureIMGUI(material.m_AlbedoTexture, "Albedo Texture");
            UpdateTextureIMGUI(material.m_NormalTexture, "Normal Texture");
            UpdateTextureIMGUI(material.m_MetallicRoughnessTexture, "Metallic Roughness Texture");

            ImGui::SliderFloat3("Const Diffuse", (float*)&material.m_ConstDiffuse, 0.0f, 1.0f);
            ImGui::Checkbox("Enable Alpha Blend", &material.m_EnableAlphaBlend);

            ImGui::Unindent();
        }
    }
}

void Visual::OnSceneLoad()
{
    InsertPrimitivesToScene();
}

void Visual::InsertPrimitivesToScene()
{
    Scene* scene = g_Graphic.m_Scene.get();

    const Matrix worldMatrix = scene->m_Nodes.at(m_NodeID).MakeLocalToWorldMatrix();

    for (Primitive& p : m_Primitives)
    {
        assert(p.IsValid());

        Mesh& primitiveMesh = g_Graphic.m_Meshes.at(p.m_MeshIdx);
        assert(primitiveMesh.IsValid());

        assert(p.m_ScenePrimitiveIndex == UINT_MAX);

        p.m_ScenePrimitiveIndex = scene->InsertPrimitive(&p, worldMatrix);
    }
}

void Visual::UpdatePrimitivesInScene()
{
    Scene* scene = g_Graphic.m_Scene.get();

    const Matrix worldMatrix = scene->m_Nodes.at(m_NodeID).MakeLocalToWorldMatrix();

    for (Primitive& p : m_Primitives)
    {
        assert(p.IsValid());

        Mesh& primitiveMesh = g_Graphic.m_Meshes.at(p.m_MeshIdx);
        assert(primitiveMesh.IsValid());

        assert(p.m_ScenePrimitiveIndex != UINT_MAX);

        scene->UpdatePrimitive(&p, worldMatrix, p.m_ScenePrimitiveIndex);
    }
}

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

    LOG_DEBUG("Mesh: [%s][V: %d][I: %d]", meshName.data(), m_NbVertices, m_NbIndices);
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

bool Material::IsValid() const
{
    bool bResult = true;

    if (m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        bResult &= m_AlbedoTexture;
    }
    if (m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        bResult &= m_NormalTexture;
    }
    if (m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        bResult &= m_MetallicRoughnessTexture;
    }

    bResult &= m_MaterialDataBufferIdx != UINT_MAX;

    return bResult;
}

void Node::UpdateIMGUI()
{
    bool bTransformDirty = false;

    ImGui::InputText("Name", &m_Name, ImGuiInputTextFlags_EnterReturnsTrue);
    bTransformDirty |= ImGui::InputFloat3("Position", (float*)&m_Position, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
    bTransformDirty |= ImGui::InputFloat3("Scale", (float*)&m_Scale, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
    //ImGui::SliderFloat3("World Rotation (yaw, pitch, roll)", (float*)&m_Rotation, 0.0f, PI, "%.2f");
    if (ImGui::SliderFloat4("World Rotation (Quaternion)", (float*)&m_Rotation, 0.0f, std::numbers::pi, "%.2f"))
    {
        bTransformDirty = true;
        m_Rotation.Normalize();
    }

    ImGui::Separator();

    const Matrix worldMatrix = MakeLocalToWorldMatrix();

    AABB aabb;
	m_AABB.Transform(aabb, worldMatrix);
    ImGui::Text("AABB Center: [%f, %f, %f]", aabb.Center.x, aabb.Center.y, aabb.Center.z);
    ImGui::Text("AABB Extents: [%f, %f, %f]", aabb.Extents.x, aabb.Extents.y, aabb.Extents.z);

    const Sphere bs = MakeLocalToWorldSphere(m_BoundingSphere, worldMatrix);
    ImGui::Text("BS Center: [%f, %f, %f]", bs.Center.x, bs.Center.y, bs.Center.z);
    ImGui::Text("BS Radius: [%f]", bs.Radius);

    if (bTransformDirty && m_VisualIdx != UINT_MAX)
    {
        Scene* scene = g_Graphic.m_Scene.get();
        scene->m_Visuals.at(m_VisualIdx).UpdatePrimitivesInScene();

        for (uint32_t childrenNodeID : m_ChildrenNodeIDs)
        {
			Node& child = scene->m_Nodes.at(childrenNodeID);

            if (child.m_VisualIdx == UINT_MAX)
            {
                continue;
            }

            scene->m_Visuals.at(child.m_VisualIdx).UpdatePrimitivesInScene();
        }
    }
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

uint32_t g_CurrentlySelectedNodeID = UINT_MAX;

static void NodeIMGUIWidget(Node& node, bool bIsNodeList)
{
    assert(node.m_ID != UINT_MAX);

    ImGui::PushID(node.m_ID);

    const char* nodeName = StringFormat("%s (ID: %d)", node.m_Name.c_str(), node.m_ID);

    // only print node name on "top bar"
    if (!bIsNodeList)
    {
        ImGui::Text(nodeName);
    }

    // selectable text button
    else if (ImGui::Selectable(nodeName, g_CurrentlySelectedNodeID == node.m_ID))
    {
        g_CurrentlySelectedNodeID = node.m_ID;
    }

    ImGui::PopID();
}

static void RenderIMGUINodeList()
{
    static std::string s_NodeNameSearchText;
    ImGui::Text("Name Filter:");
    ImGui::SameLine();
    ImGui::InputText("##NameFilter", &s_NodeNameSearchText);
    ImGui::Separator();

    std::vector<Node*> s_FinalFilteredNodes;

    std::vector<Node>& allNodes = g_Graphic.m_Scene->m_Nodes;

    for (Node& node : allNodes)
    {
        bool bPassFilter = false;

        // filter by name
        if (!s_NodeNameSearchText.empty())
        {
            std::string loweredSearchTxt = s_NodeNameSearchText;
            StringUtils::ToLower(loweredSearchTxt);

            std::string nodeNameLowered = node.m_Name;
            StringUtils::ToLower(nodeNameLowered);

            if (nodeNameLowered.find(loweredSearchTxt.c_str()) != std::string::npos)
            {
                bPassFilter = true;
            }
        }
        else
        {
            bPassFilter = true;
        }

        if (bPassFilter)
        {
            s_FinalFilteredNodes.push_back(&node);
        }
    }

    ImGui::Text("%lu Node(s) Matching:", s_FinalFilteredNodes.size());

    if (ImGui::BeginChild("Node List"))
    {
        for (Node* node : s_FinalFilteredNodes)
        {
            NodeIMGUIWidget(*node, true);
        }
    }
    ImGui::EndChild();
}

static void RenderEditorForCurrentlySelectedNode()
{
    if (g_CurrentlySelectedNodeID == UINT_MAX)
    {
        return;
    }

    ImGui::TextUnformatted("Editing:");
    ImGui::SameLine();

    Scene* scene = g_Graphic.m_Scene.get();

    Node& currentlySelectedNode = scene->m_Nodes.at(g_CurrentlySelectedNodeID);

    NodeIMGUIWidget(currentlySelectedNode, false);

    const Matrix worldMatrix = currentlySelectedNode.MakeLocalToWorldMatrix();

    AABB aabb;
	currentlySelectedNode.m_AABB.Transform(aabb, worldMatrix);

    //We not need to divise scale because it's based on the half extention of the AABB
    const Vector3 aabbMins = Vector3{ aabb.Center } - Vector3{ aabb.Extents };
    const Vector3 aabbMaxs = Vector3{ aabb.Center } + Vector3{ aabb.Extents };
    dd::aabb((float*)&aabbMins, (float*)&aabbMaxs, dd::colors::White, 0, false);

    ImGui::PushID(g_CurrentlySelectedNodeID);

    if (currentlySelectedNode.m_VisualIdx != UINT_MAX)
    {
        ImGui::Indent(30.f);
        ImGui::PushID("Widget");
		scene->m_Visuals.at(currentlySelectedNode.m_VisualIdx).UpdateIMGUI();
        ImGui::PopID();
        ImGui::Unindent(30.f);
    }

    ImGui::PopID();
}

// referenced in imguimanager
void UpdateNodeEditorWindow(bool& bWindowActive)
{
    ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Node Editor", &bWindowActive))
    {
        if (ImGui::BeginChild("list", { 300, 0 }, true))
        {
            RenderIMGUINodeList();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("editor"))
        {
            RenderEditorForCurrentlySelectedNode();
        }
        ImGui::EndChild();

    }
    ImGui::End();
}
