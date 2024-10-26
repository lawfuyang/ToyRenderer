#define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#define CGLTF_IMPLEMENTATION
#include "extern/cgltf/cgltf.h"

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
    cgltf_data* m_GLTFData = nullptr;

    std::vector<std::vector<Primitive>> m_SceneMeshPrimitives;
    std::unordered_map<const cgltf_mesh*, uint32_t> m_GLTFMeshToSceneMeshPrimitivesIndex;

    std::unordered_map<const cgltf_sampler*, nvrhi::SamplerAddressMode> m_AddressModes;

    std::vector<Texture> m_SceneTextures;
    std::unordered_map<const cgltf_texture*, uint32_t> m_GLTFTextureToSceneTexturesIdx;

    std::vector<Material> m_SceneMaterials;
    std::unordered_map<const cgltf_material*, uint32_t> m_GLTFMaterialToSceneMaterialsIdx;

    ~GLTFSceneLoader()
    {
        if (m_GLTFData)
        {
            cgltf_free(m_GLTFData);
        }
    }

    void LoadScene(std::string_view filePath)
    {
        PROFILE_FUNCTION();

        cgltf_options options{};
        
        {
            PROFILE_SCOPED("Load gltf file");
            SCOPED_TIMER_NAMED("Load gltf file");
            cgltf_result result = cgltf_parse_file(&options, filePath.data(), &m_GLTFData);

            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to load '%s': [%s]", filePath.data(), EnumUtils::ToString(result));
                return;
            }
        }

        {
            PROFILE_SCOPED("Load gltf buffers");
            SCOPED_TIMER_NAMED("Load gltf buffers");

            cgltf_result result = cgltf_load_buffers(&options, m_GLTFData, filePath.data());
            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to load buffers '%s': [%s]", filePath.data(), EnumUtils::ToString(result));
                return;
            }
        }

        m_BaseFolderPath = std::filesystem::path{ filePath }.parent_path().string();

        LoadSamplers();
        LoadTextures();
        LoadMaterials();
        LoadMeshes();
        LoadNodes();
    }

    void LoadSamplers()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        for (uint32_t i = 0; i < m_GLTFData->samplers_count; ++i)
        {
            const cgltf_sampler& gltfSampler = m_GLTFData->samplers[i];

            auto GLtoTextureAddressMode = [](int wrapMode)
                {
                    // copied from tiny_gltf
                    static const uint32_t kRepeat = 10497;
                    static const uint32_t kClampToEdge = 33071;
                    static const uint32_t kMirroredRepeat = 33648;

                    switch (wrapMode)
                    {
                    case kClampToEdge: return nvrhi::SamplerAddressMode::Clamp;
                    case kMirroredRepeat: return nvrhi::SamplerAddressMode::Mirror;
                    case kRepeat: return nvrhi::SamplerAddressMode::Wrap;
                    }
                    
                    assert(0);

                    return nvrhi::SamplerAddressMode::Clamp;
                };

            const nvrhi::SamplerAddressMode addressModeS = GLtoTextureAddressMode(gltfSampler.wrap_s);
            const nvrhi::SamplerAddressMode addressModeT = GLtoTextureAddressMode(gltfSampler.wrap_t);

            // TODO: support different S&T address modes?
            assert(addressModeS == addressModeT);

            assert(!m_AddressModes.contains(&gltfSampler));
            m_AddressModes[&gltfSampler] = addressModeS;
        }
    }

    void LoadTextures()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        if (m_GLTFData->textures_count == 0)
        {
            return;
        }

        tf::Taskflow taskflow;

        // hacky assumption that images_count == textures_count
        assert(m_GLTFData->images_count == m_GLTFData->textures_count);

        m_SceneTextures.resize(m_GLTFData->textures_count);

        for (uint32_t i = 0; i < m_GLTFData->textures_count; ++i)
        {
            m_GLTFTextureToSceneTexturesIdx[&m_GLTFData->textures[i]] = i;

            taskflow.emplace([&, i]()
                {
                    const cgltf_texture& gltfTexture = m_GLTFData->textures[i];

                    if (gltfTexture.sampler)
                    {
                        m_SceneTextures[i].m_AddressMode = m_AddressModes.at(gltfTexture.sampler);
                    }

                    const cgltf_image* image = gltfTexture.has_basisu ? gltfTexture.basisu_image : gltfTexture.image;

                    const char* debugName = image->name ? image->name : gltfTexture.name ? gltfTexture.name : "Un-Named Texture";

                    if (image->buffer_view)
                    {
                        debugName = !debugName ? image->buffer_view->name : debugName;
                        m_SceneTextures[i].LoadFromMemory((std::byte*)image->buffer_view->buffer->data + image->buffer_view->offset, image->buffer_view->size, gltfTexture.has_basisu, debugName);
                    }
                    else
                    {
                        std::string filePath = (std::filesystem::path{ m_BaseFolderPath } / image->uri).string();
                        cgltf_decode_uri(filePath.data());

                        const bool bResult = m_SceneTextures[i].LoadFromFile(filePath);
                        assert(bResult);
                    }
                });
        }

        g_Engine.m_Executor->run(taskflow).wait();
    }

    void LoadMaterials()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        m_SceneMaterials.resize(m_GLTFData->materials_count);

        for (uint32_t i = 0; i < m_GLTFData->materials_count; ++i)
        {
            Material& sceneMaterial = m_SceneMaterials[i];

            const cgltf_material& gltfMaterial = m_GLTFData->materials[i];

            if (gltfMaterial.has_pbr_metallic_roughness)
            {
                if (gltfMaterial.pbr_metallic_roughness.base_color_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseDiffuseTexture;
                    sceneMaterial.m_AlbedoTexture = m_SceneTextures.at(m_GLTFTextureToSceneTexturesIdx.at(gltfMaterial.pbr_metallic_roughness.base_color_texture.texture));
                }
                if (gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                    sceneMaterial.m_MetallicRoughnessTexture = m_SceneTextures.at(m_GLTFTextureToSceneTexturesIdx.at(gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture.texture));
                }

                sceneMaterial.m_ConstDiffuse.x = gltfMaterial.pbr_metallic_roughness.base_color_factor[0];
                sceneMaterial.m_ConstDiffuse.y = gltfMaterial.pbr_metallic_roughness.base_color_factor[1];
                sceneMaterial.m_ConstDiffuse.z = gltfMaterial.pbr_metallic_roughness.base_color_factor[2];
                sceneMaterial.m_ConstMetallic = gltfMaterial.pbr_metallic_roughness.metallic_factor;
                sceneMaterial.m_ConstRoughness = gltfMaterial.pbr_metallic_roughness.roughness_factor;
            }
            else if (gltfMaterial.has_pbr_specular_glossiness)
            {
                assert(0); // TODO?
            }

            if (gltfMaterial.normal_texture.texture)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseNormalTexture;
                sceneMaterial.m_NormalTexture = m_SceneTextures.at(m_GLTFTextureToSceneTexturesIdx.at(gltfMaterial.normal_texture.texture));
            }

            MaterialData materialData{};
            materialData.m_ConstDiffuse = sceneMaterial.m_ConstDiffuse;
            materialData.m_MaterialFlags = sceneMaterial.m_MaterialFlags;
            materialData.m_AlbedoTextureSamplerAndDescriptorIndex = (sceneMaterial.m_AlbedoTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_AlbedoTexture.m_AddressMode) << 30));
            materialData.m_NormalTextureSamplerAndDescriptorIndex = (sceneMaterial.m_NormalTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_NormalTexture.m_AddressMode) << 30));
            materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex = (sceneMaterial.m_MetallicRoughnessTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_MetallicRoughnessTexture.m_AddressMode) << 30));
            materialData.m_ConstRoughness = sceneMaterial.m_ConstRoughness;
            materialData.m_ConstMetallic = sceneMaterial.m_ConstMetallic;

            sceneMaterial.m_MaterialDataBufferIdx = g_Graphic.AppendOrRetrieveMaterialDataIndex(materialData);

            m_GLTFMaterialToSceneMaterialsIdx[&gltfMaterial] = i;
        }
    }

    void LoadMeshes()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        uint32_t nbPrimitives = 0;
        for (uint32_t i = 0; i < m_GLTFData->meshes_count; ++i)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[i];
            nbPrimitives += mesh.primitives_count;
        }

        g_Graphic.m_Meshes.reserve(g_Graphic.m_Meshes.size() + nbPrimitives);

        tf::Taskflow taskflow;

        m_SceneMeshPrimitives.resize(m_GLTFData->meshes_count);
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[modelMeshIdx];

            m_SceneMeshPrimitives[modelMeshIdx].resize(mesh.primitives_count);
            m_GLTFMeshToSceneMeshPrimitivesIndex[&mesh] = modelMeshIdx;

            for (uint32_t primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
            {
                taskflow.emplace([&, modelMeshIdx, primitiveIdx]
                    {
                        PROFILE_SCOPED("Load Primitive");

                        const cgltf_primitive& gltfPrimitive = mesh.primitives[primitiveIdx];
                        assert(gltfPrimitive.type == cgltf_primitive_type_triangles); // TODO: support more primitive topologies

                        std::vector<Graphic::IndexBufferFormat_t> indices;
                        indices.resize(gltfPrimitive.indices->count);

                        static const int indexMap[] = { 0, 1, 2 }; // if CCW, { 0, 2, 1 }
                        for (size_t i = 0; i < gltfPrimitive.indices->count; i += 3)
                        {
                            indices[i + 0] = cgltf_accessor_read_index(gltfPrimitive.indices, i + indexMap[0]);
                            indices[i + 1] = cgltf_accessor_read_index(gltfPrimitive.indices, i + indexMap[1]);
                            indices[i + 2] = cgltf_accessor_read_index(gltfPrimitive.indices, i + indexMap[2]);
                        }

                        std::vector<Vector3> vertexPositions;
                        std::vector<Vector3> vertexNormals;
                        std::vector<Vector2> vertexUVs;

                        for (size_t attrIdx = 0; attrIdx < gltfPrimitive.attributes_count; ++attrIdx)
                        {
                            const cgltf_attribute& attribute = gltfPrimitive.attributes[attrIdx];

                            if (attribute.type == cgltf_attribute_type_position)
                            {
                                vertexPositions.resize(attribute.data->count);
                                const cgltf_size unpackResult = cgltf_accessor_unpack_floats(attribute.data, &vertexPositions[0].x, attribute.data->count * 3);
                                assert(unpackResult > 0);
                            }
                            else if (attribute.type == cgltf_attribute_type_normal)
                            {
                                vertexNormals.resize(attribute.data->count);
                                const cgltf_size unpackResult = cgltf_accessor_unpack_floats(attribute.data, &vertexNormals[0].x, attribute.data->count * 3);
                                assert(unpackResult > 0);
                            }
                            else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
                            {
                                vertexUVs.resize(attribute.data->count);
                                const cgltf_size unpackResult = cgltf_accessor_unpack_floats(attribute.data, &vertexUVs[0].x, attribute.data->count * 3);
                                assert(unpackResult > 0);
                            }

                            // TODO: cgltf_attribute_type_weights, cgltf_attribute_type_joints
                        }

                        std::vector<RawVertexFormat> vertices;
                        vertices.resize(vertexPositions.size());

                        for (size_t i = 0; i < vertices.size(); ++i)
                        {
                            vertices[i].m_Position = vertexPositions[i];

                            if (!vertexNormals.empty())
                            {
                                vertices[i].m_Normal = vertexNormals[i];
                            }

                            if (!vertexUVs.empty())
                            {
                                vertices[i].m_TexCoord = vertexUVs[i];
                            }
                        }

                        uint32_t meshIdx;
                        Mesh* sceneMesh = nullptr;
                        bool bRetrievedFromCache = false;
                        g_Graphic.GetOrCreateMesh(Mesh::HashVertices(vertices), meshIdx, sceneMesh, bRetrievedFromCache);

                        if (!bRetrievedFromCache)
                        {
                            sceneMesh->Initialize(vertices, indices, m_GLTFData->meshes[modelMeshIdx].name ? m_GLTFData->meshes[modelMeshIdx].name : "Un-named Mesh");
                        }

                        Primitive& primitive = m_SceneMeshPrimitives[modelMeshIdx][primitiveIdx];
                        if (gltfPrimitive.material)
                        {
                            primitive.m_Material = m_SceneMaterials.at(m_GLTFMaterialToSceneMaterialsIdx.at(gltfPrimitive.material));
                        }
                        else
                        {
                            primitive.m_Material = g_CommonResources.DefaultMaterial;
                        }
                        primitive.m_MeshIdx = meshIdx;

                        assert(primitive.IsValid());
                    });
            }
        }

        // Multi-thread load IO init mesh
        g_Engine.m_Executor->run(taskflow).wait();
    }

    void AddNodeToScene(
        const cgltf_node& node,
        const Vector3& scale,
        const Vector3& translation,
        const Quaternion& rotation,
        const std::unordered_map<const cgltf_node*, uint32_t>& nodeToSceneNodeIndex
    )
    {
        Scene* scene = g_Graphic.m_Scene.get();
        const uint32_t newNodeID = scene->m_Nodes.size();

        Node& newNode = scene->m_Nodes.emplace_back();
        newNode.m_ID = newNodeID;
        newNode.m_Name = node.name ? node.name : "Un-named Mode";
        newNode.m_Position = translation;
        newNode.m_Rotation = rotation;
        newNode.m_Scale = scale;

        if (node.parent)
        {
            newNode.m_ParentNodeID = nodeToSceneNodeIndex.at(node.parent);
        }

        for (uint32_t i = 0; i < node.children_count; ++i)
        {
            newNode.m_ChildrenNodeIDs.push_back(nodeToSceneNodeIndex.at(node.children[i]));
        }

        if (node.mesh)
        {
            const uint32_t visualIdx = scene->m_Visuals.size();

            Visual& newVisual = scene->m_Visuals.emplace_back();
            newVisual.m_NodeID = newNodeID;
            newVisual.m_Name = node.name ? node.name : "Un-named Visual";

            const uint32_t sceneMeshPrimitivesIdx = m_GLTFMeshToSceneMeshPrimitivesIndex.at(node.mesh);

            for (const Primitive& primitive : m_SceneMeshPrimitives.at(sceneMeshPrimitivesIdx))
            {
                newVisual.m_Primitives.push_back(primitive);
                newVisual.m_Primitives.back().m_VisualIdx = visualIdx;

                Mesh& primitiveMesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

                AABB::CreateMerged(newNode.m_AABB, newNode.m_AABB, primitiveMesh.m_AABB);
                Sphere::CreateMerged(newNode.m_BoundingSphere, newNode.m_BoundingSphere, primitiveMesh.m_BoundingSphere);
            }

            newNode.m_VisualIdx = visualIdx;
        }
    }

    void LoadNodes()
    {
        PROFILE_FUNCTION();
        SCOPED_TIMER_FUNCTION();

        Scene* scene = g_Graphic.m_Scene.get();

        std::unordered_map<const cgltf_node*, uint32_t> nodeToSceneNodeIndex;

        // init node maps first
        for (uint32_t i = 0; i < m_GLTFData->nodes_count; ++i)
        {
            const cgltf_node& node = m_GLTFData->nodes[i];
            nodeToSceneNodeIndex[&node] = i;
        }

        for (uint32_t i = 0; i < m_GLTFData->nodes_count; ++i)
        {
            const cgltf_node& node = m_GLTFData->nodes[i];
            nodeToSceneNodeIndex[&node] = i;

            if (node.has_mesh_gpu_instancing)
            {
                std::vector<Vector3> instanceScales;
                std::vector<Vector3> instanceTranslations;
                std::vector<Quaternion> instanceRotations;

                for (uint32_t j = 0; j < node.mesh_gpu_instancing.attributes_count; ++j)
                {
                    const cgltf_attribute& instanceAttribute = node.mesh_gpu_instancing.attributes[j];

                    if (strcmp(instanceAttribute.name, "TRANSLATION") == 0)
                    {
                        instanceTranslations.resize(instanceAttribute.data->count);
                        const cgltf_size unpackResult = cgltf_accessor_unpack_floats(instanceAttribute.data, &instanceTranslations[0].x, instanceAttribute.data->count * 3);
                        assert(unpackResult > 0);
                    }
                    else if (strcmp(instanceAttribute.name, "ROTATION") == 0)
                    {
                        instanceRotations.resize(instanceAttribute.data->count);
                        const cgltf_size unpackResult = cgltf_accessor_unpack_floats(instanceAttribute.data, &instanceRotations[0].x, instanceAttribute.data->count * 4);
                        assert(unpackResult > 0);
                    }
                    else if (strcmp(instanceAttribute.name, "SCALE") == 0)
                    {
                        instanceScales.resize(instanceAttribute.data->count);
                        const cgltf_size unpackResult = cgltf_accessor_unpack_floats(instanceAttribute.data, &instanceScales[0].x, instanceAttribute.data->count * 3);
                        assert(unpackResult > 0);
                    }
                }

                const uint32_t nbInstances = instanceTranslations.size();
                for (uint32_t instanceIdx = 0; instanceIdx < nbInstances; ++instanceIdx)
                {
                    const Vector3& scale = instanceScales[instanceIdx];
                    const Vector3& translation = instanceTranslations[instanceIdx];
                    const Quaternion& rotation = instanceRotations[instanceIdx];

                    AddNodeToScene(node, scale, translation, rotation, nodeToSceneNodeIndex);
                }
            }
            else
            {
                const Vector3 scale{ node.scale[0], node.scale[1], node.scale[2] };
                const Vector3 translation{ node.translation[0], node.translation[1], node.translation[2] };
                const Quaternion rotation{ node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] };

                AddNodeToScene(node, scale, translation, rotation, nodeToSceneNodeIndex);
            }
        }

        // need to be a separate loop due to 'MakeLocalToWorldMatrix' requiring all nodes to be loaded
        for (Node& node : scene->m_Nodes)
        {
            // update scene BS too
            const Matrix nodeWorldMatrix = node.MakeLocalToWorldMatrix();

            AABB nodeAABB;
            node.m_AABB.Transform(nodeAABB, nodeWorldMatrix);

            AABB::CreateMerged(scene->m_AABB, scene->m_AABB, nodeAABB);

            Sphere nodeBS;
            node.m_BoundingSphere.Transform(nodeBS, nodeWorldMatrix);

            Sphere::CreateMerged(scene->m_BoundingSphere, scene->m_BoundingSphere, nodeBS);
        }
    }
};

void LoadScene(std::string_view filePath)
{
    SCOPED_TIMER_FUNCTION();

    GLTFSceneLoader loader;
    loader.LoadScene(filePath);

    if (g_ProfileSceneLoading.Get())
    {
        Engine::TriggerDumpProfilingCapture("SceneLoad");
    }
}
