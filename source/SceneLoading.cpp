#include "extern/json/json.hpp"
#include "extern/json/tiny_gltf.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "SmallVector.h"
#include "Utilities.h"
#include "Visual.h"

#include "shaders/shared/CommonConsts.h"
#include "shaders/shared/RawVertexFormat.h"
#include "shaders/shared/MaterialData.h"

CommandLineOption<bool> g_ProfileSceneLoading{ "profilesceneloading", false };

struct GLTFSceneLoader
{
    std::string m_BaseFolderPath;
    tinygltf::Model m_Model;

    std::vector<std::vector<Primitive>> m_SceneMeshPrimitives;
    std::vector<nvrhi::SamplerAddressMode> m_AddressModes;
    std::vector<Texture> m_SceneTextures;
    std::vector<Material> m_SceneMaterials;

    void LoadScene(std::string_view filePath)
    {
        PROFILE_FUNCTION();

        m_BaseFolderPath = std::filesystem::path{ filePath }.parent_path().string();

        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(&LoadImageDataFunction, this);

        std::string err, warn;
        bool ret = false;
        {
            PROFILE_SCOPED("Load file from disk");
            SCOPED_TIMER_NAMED("Load file from disk");

            const char* fileExt = GetFileExtensionFromPath(filePath);
            if (std::strcmp(fileExt, ".gltf") == 0)
            {
                ret = loader.LoadASCIIFromFile(&m_Model, &err, &warn, filePath.data());
            }
            else if (std::strcmp(fileExt, ".glb") == 0)
            {
                ret = loader.LoadBinaryFromFile(&m_Model, &err, &warn, filePath.data());
            }
        }

        if (!err.empty())
        {
            LOG_DEBUG("GLTF Load Error: %s", err.c_str());
        }

        if (!warn.empty())
        {
            LOG_DEBUG("GLTF Load Warning: %s", err.c_str());
        }

        if (!ret)
        {
            return;
        }

        LoadSamplers();
        LoadTextures();
        LoadMaterials();
        LoadMeshes();
        LoadNodes();
    }

    static bool LoadImageDataFunction(tinygltf::Image* image, const int idx, std::string* err, std::string* warn, int width, int height, const unsigned char* dataPtr, int dataSize, void* user_pointer)
    {
        // simply copy over the image bytes into the tinygltf::Image obj itself to defer load the texture in 'LoadTextures'
        image->image.resize(dataSize);
        memcpy(image->image.data(), dataPtr, dataSize);

        return true;
    }

    void LoadSamplers()
    {
        PROFILE_FUNCTION();

        m_AddressModes.resize(m_Model.samplers.size());

        for (uint32_t i = 0; i < m_Model.samplers.size(); ++i)
        {
            const tinygltf::Sampler& sampler = m_Model.samplers[i];

            auto GLtoTextureAddressMode = [](int glWrapMode)
                {
                    switch (glWrapMode)
                    {
                    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: return nvrhi::SamplerAddressMode::Clamp;
                    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: return nvrhi::SamplerAddressMode::Mirror;
                    case TINYGLTF_TEXTURE_WRAP_REPEAT: return nvrhi::SamplerAddressMode::Wrap;
                    }

                    return nvrhi::SamplerAddressMode::Clamp;
                };

            const nvrhi::SamplerAddressMode addressModeS = GLtoTextureAddressMode(sampler.wrapS);
            const nvrhi::SamplerAddressMode addressModeT = GLtoTextureAddressMode(sampler.wrapT);

            // TODO: support different S&T address modes?
            assert(addressModeS == addressModeT);

            m_AddressModes[i] = addressModeS;
        }
    }

    void LoadTextures()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        m_SceneTextures.resize(m_Model.textures.size());

        struct DeferLoadTexture
        {
            Texture m_Texture;
            uint32_t m_Idx;
        };
        std::vector<DeferLoadTexture> deferLoadTextures;

        tf::Taskflow taskflow;
        for (uint32_t i = 0; i < m_Model.textures.size(); ++i)
        {
            taskflow.emplace([&, i] () mutable
                {
                    const tinygltf::Texture& texture = m_Model.textures[i];

                    int textureSourceIdx = -1;
                    const auto KTXExtIt = texture.extensions.find("KHR_texture_basisu");
                    if (KTXExtIt != texture.extensions.end())
                    {
                        textureSourceIdx = KTXExtIt->second.Get("source").GetNumberAsInt();
                    }
                    else
                    {
                        textureSourceIdx = texture.source;
                    }

                    const tinygltf::Image& image = m_Model.images.at(textureSourceIdx);

                    Texture newTex;

                    if (texture.sampler > -1)
                    {
                        newTex.m_AddressMode = m_AddressModes.at(texture.sampler);
                    }

                    // if the image's internal data array is filled, it means it went through the 'LoadImageDataFunction'. i.e, its embedded in the .glb itself
                    if (!image.image.empty())
                    {
                        const bool bIsKTX2 = image.mimeType == "image/ktx2";

                        newTex.LoadFromMemory(image.image.data(), (uint32_t)image.image.size(), bIsKTX2, image.name);
                        m_SceneTextures[i] = newTex;
                    }
                    else
                    {
                        std::string filePath = (std::filesystem::path{ m_BaseFolderPath } / image.uri.c_str()).string();
                        filePath = std::regex_replace(filePath, std::regex("%20"), " "); // uri is not decoded (e.g. whitespace may be represented as %20)

                        m_SceneTextures[i] = newTex;

                        // loading from file might fail if another thread is proceesing it, so we load it from cache after all textures are loaded multi-threaded
                        if (newTex.LoadFromFile(filePath))
                        {
                            m_SceneTextures[i] = newTex;
                        }
                        else
                        {
                            static std::mutex s_Lck;
                            AUTO_LOCK(s_Lck);

                            deferLoadTextures.push_back({ newTex, i });
                        }
                    }
                });
        }

        // Multi-thread IO load & init textures
        if (!taskflow.empty())
        {
            g_Engine.m_Executor->run(taskflow).wait();
        }

        for (DeferLoadTexture& deferLoadTex : deferLoadTextures)
        {
            const bool bResult = deferLoadTex.m_Texture.LoadFromCache();

            // at this point, whatever that was loaded in parallel must have already been loaded here
            assert(bResult && deferLoadTex.m_Texture);

            m_SceneTextures[deferLoadTex.m_Idx] = deferLoadTex.m_Texture;
        }
    }

    void LoadMaterials()
    {
        PROFILE_FUNCTION();

        // TODO: multi-thread if needed
        m_SceneMaterials.resize(m_Model.materials.size());
        for (uint32_t i = 0; i < m_Model.materials.size(); ++i)
        {
            PROFILE_SCOPED("Load Material");

            const tinygltf::Material& material = m_Model.materials.at(i);
            const tinygltf::NormalTextureInfo& normalTextureInfo = material.normalTexture;
            const tinygltf::PbrMetallicRoughness& pbrMetallicRoughness = material.pbrMetallicRoughness;

            Material& sceneMaterial = m_SceneMaterials[i];

            if (pbrMetallicRoughness.baseColorTexture.index != -1)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseDiffuseTexture;
                sceneMaterial.m_AlbedoTexture = m_SceneTextures[pbrMetallicRoughness.baseColorTexture.index];
            }
            if (normalTextureInfo.index != -1)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseNormalTexture;
                sceneMaterial.m_NormalTexture = m_SceneTextures[normalTextureInfo.index];
            }
            if (pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                sceneMaterial.m_MetallicRoughnessTexture = m_SceneTextures[pbrMetallicRoughness.metallicRoughnessTexture.index];
            }

            sceneMaterial.m_ConstDiffuse = ToMathType<Vector3>(pbrMetallicRoughness.baseColorFactor);
            sceneMaterial.m_ConstRoughness = (float)pbrMetallicRoughness.roughnessFactor;
            sceneMaterial.m_ConstMetallic = (float)pbrMetallicRoughness.metallicFactor;

            // give the float values discreet steps (8-bit compressed) to allow them to be hash friendly
            const Vector3U compressedConstDiffuseUint{ (uint32_t)(sceneMaterial.m_ConstDiffuse.x * 255.0f), (uint32_t)(sceneMaterial.m_ConstDiffuse.y * 255.0f), (uint32_t)(sceneMaterial.m_ConstDiffuse.z * 255.0f) };
            const Vector3 compressedConstDiffuse{ (float)compressedConstDiffuseUint.x / 255.0f, (float)compressedConstDiffuseUint.y / 255.0f, (float)compressedConstDiffuseUint.z / 255.0f };
            const float compressedConstRoughness = std::floor(sceneMaterial.m_ConstRoughness * 255.0f) / 255.0f;
            const float compressedConstMetallic = std::floor(sceneMaterial.m_ConstMetallic * 255.0f) / 255.0f;

            MaterialData materialData{};
            materialData.m_ConstDiffuse = compressedConstDiffuse;
            materialData.m_MaterialFlags = sceneMaterial.m_MaterialFlags;
            materialData.m_AlbedoTextureSamplerAndDescriptorIndex = (sceneMaterial.m_AlbedoTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_AlbedoTexture.m_AddressMode) << 30));
            materialData.m_NormalTextureSamplerAndDescriptorIndex = (sceneMaterial.m_NormalTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_NormalTexture.m_AddressMode) << 30));
            materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex = (sceneMaterial.m_MetallicRoughnessTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_MetallicRoughnessTexture.m_AddressMode) << 30));
            materialData.m_ConstRoughness = compressedConstRoughness;
            materialData.m_ConstMetallic = compressedConstMetallic;

            sceneMaterial.m_MaterialDataBufferIdx = g_Graphic.AppendOrRetrieveMaterialDataIndex(materialData);
        }
    }

    template <typename VertexOrIndexType>
    void LoadAttributes(std::vector<VertexOrIndexType>& attributeContainer, int attributeID, uint32_t vertexAttributeMemOffset = 0)
    {
        assert(attributeID != -1);

        const tinygltf::Accessor& accessor = m_Model.accessors.at(attributeID);
        const tinygltf::BufferView& bufferView = m_Model.bufferViews.at(accessor.bufferView);
        const tinygltf::Buffer& buffer = m_Model.buffers.at(bufferView.buffer);

        const unsigned char* dataPtr = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;

        const int byteStride = accessor.ByteStride(bufferView);
        assert(byteStride != -1);

        const uint32_t attributeSizeInBytes = tinygltf::GetComponentSizeInBytes(accessor.componentType) * tinygltf::GetNumComponentsInType(accessor.type);

        for (uint32_t i = 0; i < accessor.count; ++i, dataPtr += byteStride)
        {
            std::memcpy((std::byte*)&attributeContainer.at(i) + vertexAttributeMemOffset, dataPtr, attributeSizeInBytes);
        }
    }

    void LoadMeshes()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        tf::Taskflow taskflow;
        
        m_SceneMeshPrimitives.resize(m_Model.meshes.size());
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_Model.meshes.size(); ++modelMeshIdx)
        {
            const tinygltf::Mesh& mesh = m_Model.meshes.at(modelMeshIdx);

            m_SceneMeshPrimitives[modelMeshIdx].resize(mesh.primitives.size());
            for (uint32_t primitiveIdx = 0; primitiveIdx < mesh.primitives.size(); ++primitiveIdx)
            {
                taskflow.emplace([&, modelMeshIdx, primitiveIdx]
                    {
                        PROFILE_SCOPED("Load Primitive");

                        const tinygltf::Primitive& gltfPrimitive = mesh.primitives.at(primitiveIdx);
                        assert(gltfPrimitive.mode == TINYGLTF_MODE_TRIANGLES); // TODO: support more primitive topologies

                        // vertices
                        std::vector<RawVertexFormat> vertices;
                        const int positionIdx = gltfPrimitive.attributes.at("POSITION");
                        vertices.resize(m_Model.accessors.at(positionIdx).count);

                        auto LoadAttributesHelper = [&](std::string_view attributeString, uint32_t vertexAttributeMemOffset)
                            {
                                if (auto it = gltfPrimitive.attributes.find(attributeString.data());
                                    it != gltfPrimitive.attributes.end())
                                {
                                    LoadAttributes(vertices, it->second, vertexAttributeMemOffset);
                                }
                            };

                        LoadAttributesHelper("POSITION", offsetof(RawVertexFormat, m_Position));
                        LoadAttributesHelper("NORMAL", offsetof(RawVertexFormat, m_Normal));
                        LoadAttributesHelper("TEXCOORD_0", offsetof(RawVertexFormat, m_TexCoord));

                        // indices
                        std::vector<Graphic::IndexBufferFormat_t> indices;
                        const int indicesIdx = gltfPrimitive.indices;
                        indices.resize(m_Model.accessors.at(indicesIdx).count);

                        LoadAttributes(indices, indicesIdx);

                        PROFILE_FUNCTION();

                        bool bRetrievedFromCache = false;
                        const uint32_t meshIdx = g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), bRetrievedFromCache);

                        Mesh* sceneMesh = g_Graphic.m_Meshes.at(meshIdx);

                        if (!bRetrievedFromCache)
                        {
                            sceneMesh->Initialize(vertices, indices, mesh.name);
                        }

                        Primitive& primitive = m_SceneMeshPrimitives[modelMeshIdx][primitiveIdx];

                        primitive.m_Material = m_SceneMaterials[gltfPrimitive.material];
                        assert(primitive.m_Material.IsValid());

                        primitive.m_MeshIdx = meshIdx;
                    });
            }
        }

        // Multi-thread load IO init mesh
        g_Engine.m_Executor->run(taskflow).wait();
    }

    // convert into array of floats, then shoves it into vector/matrix
    template <typename T>
    static T ToMathType(std::span<const double> val)
    {
        SmallVector<float, sizeof(T) / sizeof(float)> arr;
        for (double d : val)
            arr.push_back((float)d);
        return T{ arr.data() };
    }

    void TraverseNode(const tinygltf::Node& gltfNode, Node* parent = nullptr)
    {
        Vector3 scale = Vector3::One;
        Vector3 translation;
        Quaternion rotation;

        // Matrix and T/R/S are exclusive
        if (!gltfNode.matrix.empty())
        {
            Matrix matrix = ToMathType<Matrix>(gltfNode.matrix);

            // decompose local matrix
            const bool bDecomposeResult = matrix.Decompose(scale, rotation, translation);
            assert(bDecomposeResult);
        }
        else
        {
            if (!gltfNode.translation.empty())
                translation = ToMathType<Vector3>(gltfNode.translation);

            if (!gltfNode.rotation.empty())
                rotation = ToMathType<Quaternion>(gltfNode.rotation);

            if (!gltfNode.scale.empty())
                scale = ToMathType<Vector3>(gltfNode.scale);
        }
        
        Scene* scene = g_Graphic.m_Scene.get();
        const uint32_t newNodeID = scene->m_Nodes.size();

        Node* newNode = scene->m_NodeAllocator.NewObject();
        newNode->m_ID = newNodeID;
        newNode->m_Name = gltfNode.name;
        newNode->m_Position = translation;
        newNode->m_Rotation = rotation;
        newNode->m_Scale = scale;

        if (parent)
        {
            newNode->m_ParentNodeID = parent->m_ID;
            parent->m_ChildrenNodeIDs.push_back(newNodeID);
        }

        scene->m_Nodes.push_back(newNode);

        // if a node does not have a mesh, it's simply an node with no Visual
        if (gltfNode.mesh != -1)
        {
            Visual* newVisual = scene->m_VisualAllocator.NewObject();
            newVisual->m_Node = newNode;
            newVisual->m_Name = gltfNode.name;

            const uint32_t visualIdx = scene->m_Visuals.size();
            scene->m_Visuals.push_back(newVisual);

            for (const Primitive& primitive : m_SceneMeshPrimitives.at(gltfNode.mesh))
            {
                newVisual->m_Primitives.push_back(primitive);
                newVisual->m_Primitives.back().m_VisualIdx = visualIdx;

				Mesh* primitiveMesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

                AABB::CreateMerged(newNode->m_AABB, newNode->m_AABB, primitiveMesh->m_AABB);
                Sphere::CreateMerged(newNode->m_BoundingSphere, newNode->m_BoundingSphere, primitiveMesh->m_BoundingSphere);
            }

            // update scene BS too
            const Matrix nodeWorldMatrix = newNode->MakeLocalToWorldMatrix();

            const AABB nodeAABB = MakeLocalToWorldAABB(newNode->m_AABB, nodeWorldMatrix);
            AABB::CreateMerged(scene->m_AABB, scene->m_AABB, nodeAABB);

            const Sphere nodeBS = MakeLocalToWorldSphere(newNode->m_BoundingSphere, nodeWorldMatrix);
            Sphere::CreateMerged(scene->m_BoundingSphere, scene->m_BoundingSphere, nodeBS);

            newNode->m_VisualIdx = visualIdx;
        }

        if (gltfNode.camera != -1)
        {
            // TODO
        }

        for (int childIdx : gltfNode.children)
        {
            TraverseNode(m_Model.nodes[childIdx], newNode);
        }
    }

    void LoadNodes()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();
        
        // TODO: support more than 1 scene?
        assert(m_Model.scenes.size() == 1);
        const tinygltf::Scene& scene = m_Model.scenes.at(0);

        for (int nodeIdx : scene.nodes)
        {
            const tinygltf::Node& gltfNode = m_Model.nodes.at(nodeIdx);
            TraverseNode(gltfNode);
        }
    }
};

void LoadScene(std::string_view filePath)
{
    SCOPED_TIMER_FUNCTION();

    // super special path for glb files, because Assimp just doesn't like .glb for some reason
    std::string fileExt = GetFileExtensionFromPath(filePath);
    StringUtils::ToLower(fileExt);

    GLTFSceneLoader loader;
    loader.LoadScene(filePath);

    if (g_ProfileSceneLoading.Get())
    {
        Engine::TriggerDumpProfilingCapture("SceneLoad");
    }
}
