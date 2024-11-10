#define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#define CGLTF_IMPLEMENTATION
#include "extern/cgltf/cgltf.h"

#include "extern/meshoptimizer/meshoptimizer.h"

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

#define SCENE_LOAD_PROFILE(x) \
    PROFILE_SCOPED(x);        \
    SCOPED_TIMER_NAMED(x);

struct GLTFSceneLoader
{
    std::string m_BaseFolderPath;
    cgltf_data* m_GLTFData = nullptr;

    std::vector<nvrhi::SamplerAddressMode> m_AddressModes;
    std::vector<std::vector<Primitive>> m_SceneMeshPrimitives;
    std::vector<Texture> m_SceneImages;
    std::vector<Material> m_SceneMaterials;

    void LoadScene(std::string_view filePath)
    {
        SCENE_LOAD_PROFILE("Load Scene");

        ON_EXIT_SCOPE_LAMBDA([this] { cgltf_free(m_GLTFData); });

        cgltf_options options{};
        
        {
            SCENE_LOAD_PROFILE("Load gltf file");

            cgltf_result result = cgltf_parse_file(&options, filePath.data(), &m_GLTFData);

            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to load '%s': [%s]", filePath.data(), EnumUtils::ToString(result));
                return;
            }

            for (uint32_t i = 0; i < m_GLTFData->extensions_used_count; ++i)
            {
                // NOTE: don't support mesh_gpu_instancing. it merely reduces the nb of nodes to read, but breaks scene hierarchy and i'm lazy to investigate & fix
                if (strcmp(m_GLTFData->extensions_used[i], "EXT_mesh_gpu_instancing") == 0)
                {
                    assert(0);
                }
            }
        }

        {
            SCENE_LOAD_PROFILE("Load gltf buffers");

            cgltf_result result = cgltf_load_buffers(&options, m_GLTFData, filePath.data());
            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to load buffers '%s': [%s]", filePath.data(), EnumUtils::ToString(result));
                return;
            }
        }

        {
            SCENE_LOAD_PROFILE("Decompress buffers");

            const cgltf_result result = decompressMeshopt(m_GLTFData);
            assert(result == cgltf_result_success);
        }

        m_BaseFolderPath = std::filesystem::path{ filePath }.parent_path().string();

        LoadSamplers();
        LoadImages();
        LoadMaterials();
        LoadMeshes();
        LoadNodes();
    }

    // referred from meshoptimizer
    static cgltf_result decompressMeshopt(cgltf_data* data)
    {
        for (size_t i = 0; i < data->buffer_views_count; ++i)
        {
            if (!data->buffer_views[i].has_meshopt_compression)
                continue;
            cgltf_meshopt_compression* mc = &data->buffer_views[i].meshopt_compression;

            const unsigned char* source = (const unsigned char*)mc->buffer->data;
            if (!source)
                return cgltf_result_invalid_gltf;
            source += mc->offset;

            void* result = malloc(mc->count * mc->stride);
            if (!result)
                return cgltf_result_out_of_memory;

            data->buffer_views[i].data = result;

            int rc = -1;

            switch (mc->mode)
            {
            case cgltf_meshopt_compression_mode_attributes:
                rc = meshopt_decodeVertexBuffer(result, mc->count, mc->stride, source, mc->size);
                break;

            case cgltf_meshopt_compression_mode_triangles:
                rc = meshopt_decodeIndexBuffer(result, mc->count, mc->stride, source, mc->size);
                break;

            case cgltf_meshopt_compression_mode_indices:
                rc = meshopt_decodeIndexSequence(result, mc->count, mc->stride, source, mc->size);
                break;

            default:
                return cgltf_result_invalid_gltf;
            }

            if (rc != 0)
                return cgltf_result_io_error;

            switch (mc->filter)
            {
            case cgltf_meshopt_compression_filter_octahedral:
                meshopt_decodeFilterOct(result, mc->count, mc->stride);
                break;

            case cgltf_meshopt_compression_filter_quaternion:
                meshopt_decodeFilterQuat(result, mc->count, mc->stride);
                break;

            case cgltf_meshopt_compression_filter_exponential:
                meshopt_decodeFilterExp(result, mc->count, mc->stride);
                break;

            default:
                break;
            }
        }

        return cgltf_result_success;
    }

    void LoadSamplers()
    {
        SCENE_LOAD_PROFILE("Load Samplers");

        m_AddressModes.resize(m_GLTFData->samplers_count);
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

            m_AddressModes[i] = addressModeS;
        }
    }

    void LoadImages()
    {
        SCENE_LOAD_PROFILE("Load Images");

        if (m_GLTFData->images_count == 0)
        {
            return;
        }

        tf::Taskflow taskflow;

        m_SceneImages.resize(m_GLTFData->images_count);
        for (uint32_t i = 0; i < m_GLTFData->images_count; ++i)
        {
            taskflow.emplace([&, i]()
                {
                    const cgltf_image& image = m_GLTFData->images[i];
                    const char* debugName = image.name ? image.name : "Un-Named Image";

                    if (image.buffer_view)
                    {
                        debugName = !debugName ? image.buffer_view->name : debugName;
                        m_SceneImages[i].LoadFromMemory((std::byte*)image.buffer_view->buffer->data + image.buffer_view->offset, image.buffer_view->size, debugName);
                    }
                    else
                    {
                        std::string filePath = (std::filesystem::path{ m_BaseFolderPath } / image.uri).string();
                        cgltf_decode_uri(filePath.data());

                        m_SceneImages[i].LoadFromFile(filePath);
                    }
                });
        }

        g_Engine.m_Executor->run(taskflow).wait();
    }

    void LoadMaterials()
    {
        SCENE_LOAD_PROFILE("Load Materials");

        auto HandleTextureView = [&](Texture& texture, const cgltf_texture_view& textureView)
            {
                const cgltf_image* image = textureView.texture->has_basisu ? textureView.texture->basisu_image : textureView.texture->image;
                assert(image);

                texture = m_SceneImages.at(cgltf_image_index(m_GLTFData, image));

                if (textureView.has_transform)
                {
                    // sanity check to see if 1 texture view per image
                    assert(texture.m_UVOffset == Vector2::Zero);
                    assert(texture.m_UVScale == Vector2::One);

                    texture.m_UVOffset.x = textureView.transform.offset[0];
                    texture.m_UVOffset.y = textureView.transform.offset[1];
                    texture.m_UVScale.x = textureView.transform.scale[0];
                    texture.m_UVScale.y = textureView.transform.scale[1];
                }
            };

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
                    HandleTextureView(sceneMaterial.m_AlbedoTexture, gltfMaterial.pbr_metallic_roughness.base_color_texture);
                }
                if (gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                    HandleTextureView(sceneMaterial.m_MetallicRoughnessTexture, gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture);
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
                HandleTextureView(sceneMaterial.m_NormalTexture, gltfMaterial.normal_texture);
            }

            MaterialData materialData{};
            materialData.m_ConstDiffuse = sceneMaterial.m_ConstDiffuse;
            materialData.m_MaterialFlags = sceneMaterial.m_MaterialFlags;
            materialData.m_AlbedoTextureSamplerAndDescriptorIndex = (sceneMaterial.m_AlbedoTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_AlbedoTexture.m_AddressMode) << 30));
            materialData.m_NormalTextureSamplerAndDescriptorIndex = (sceneMaterial.m_NormalTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_NormalTexture.m_AddressMode) << 30));
            materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex = (sceneMaterial.m_MetallicRoughnessTexture.m_DescriptorIndex | (((uint32_t)sceneMaterial.m_MetallicRoughnessTexture.m_AddressMode) << 30));
            materialData.m_ConstRoughness = sceneMaterial.m_ConstRoughness;
            materialData.m_ConstMetallic = sceneMaterial.m_ConstMetallic;
            materialData.m_AlbedoUVOffset = sceneMaterial.m_AlbedoTexture.m_UVOffset;
            materialData.m_AlbedoUVScale = sceneMaterial.m_AlbedoTexture.m_UVScale;
            materialData.m_NormalUVOffset = sceneMaterial.m_NormalTexture.m_UVOffset;
            materialData.m_NormalUVScale = sceneMaterial.m_NormalTexture.m_UVScale;
            materialData.m_MetallicRoughnessUVOffset = sceneMaterial.m_MetallicRoughnessTexture.m_UVOffset;
            materialData.m_MetallicRoughnessUVScale = sceneMaterial.m_MetallicRoughnessTexture.m_UVScale;

            const uint64_t byteOffset = g_Graphic.m_VirtualMaterialDataBuffer.QueueAppend(&materialData, sizeof(MaterialData));
            sceneMaterial.m_MaterialDataBufferIdx = byteOffset / sizeof(MaterialData);
        }
    }

    void LoadMeshes()
    {
        SCENE_LOAD_PROFILE("Load Meshes");

        uint32_t nbPrimitives = 0;
        for (uint32_t i = 0; i < m_GLTFData->meshes_count; ++i)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[i];
            nbPrimitives += mesh.primitives_count;
        }

        // NOTE: due to internal assert in 'CreateMesh' we need to reserve space for all meshes
        g_Graphic.m_Meshes.reserve(g_Graphic.m_Meshes.size() + nbPrimitives);

        tf::Taskflow taskflow;

        m_SceneMeshPrimitives.resize(m_GLTFData->meshes_count);
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[modelMeshIdx];

            m_SceneMeshPrimitives[modelMeshIdx].resize(mesh.primitives_count);

            for (uint32_t primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
            {
                taskflow.emplace([&, modelMeshIdx, primitiveIdx]
                    {
                        PROFILE_SCOPED("Load Primitive");

                        const cgltf_primitive& gltfPrimitive = mesh.primitives[primitiveIdx];
                        assert(gltfPrimitive.type == cgltf_primitive_type_triangles);

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
                        std::vector<Vector4> vertexTangents;
                        std::vector<Vector2> vertexUVs;

                        for (size_t attrIdx = 0; attrIdx < gltfPrimitive.attributes_count; ++attrIdx)
                        {
                            const cgltf_attribute& attribute = gltfPrimitive.attributes[attrIdx];

                            if (attribute.type == cgltf_attribute_type_position)
                            {
                                vertexPositions.resize(attribute.data->count);
                                verify(cgltf_accessor_unpack_floats(attribute.data, &vertexPositions[0].x, attribute.data->count * 3));
                            }
                            else if (attribute.type == cgltf_attribute_type_normal)
                            {
                                vertexNormals.resize(attribute.data->count);
                                verify(cgltf_accessor_unpack_floats(attribute.data, &vertexNormals[0].x, attribute.data->count * 3));
                            }
                            else if (attribute.type == cgltf_attribute_type_tangent)
                            {
                                vertexTangents.resize(attribute.data->count);
                                verify(cgltf_accessor_unpack_floats(attribute.data, &vertexTangents[0].x, attribute.data->count * 4));
                            }
                            else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) // only read the first UV set
                            {
                                vertexUVs.resize(attribute.data->count);
                                verify(cgltf_accessor_unpack_floats(attribute.data, &vertexUVs[0].x, attribute.data->count * 3));
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

                            if (!vertexTangents.empty())
                            {
                                vertices[i].m_Tangent = vertexTangents[i];
                            }

                            if (!vertexUVs.empty())
                            {
                                vertices[i].m_TexCoord = vertexUVs[i];
                            }
                        }

                        Mesh* sceneMesh = g_Graphic.CreateMesh();
                        sceneMesh->Initialize(vertices, indices, m_GLTFData->meshes[modelMeshIdx].name ? m_GLTFData->meshes[modelMeshIdx].name : "Un-named Mesh");

                        Primitive& primitive = m_SceneMeshPrimitives[modelMeshIdx][primitiveIdx];
                        if (gltfPrimitive.material)
                        {
                            primitive.m_Material = m_SceneMaterials.at(cgltf_material_index(m_GLTFData, gltfPrimitive.material));
                        }
                        else
                        {
                            primitive.m_Material = g_CommonResources.DefaultMaterial;
                        }
                        primitive.m_MeshIdx = sceneMesh->m_Idx;
                    });
            }
        }

        g_Engine.m_Executor->run(taskflow).wait();
    }

    void LoadNodes()
    {
        SCENE_LOAD_PROFILE("Load Nodes");

        Scene* scene = g_Graphic.m_Scene.get();

        for (uint32_t i = 0; i < m_GLTFData->nodes_count; ++i)
        {
            cgltf_node& node = m_GLTFData->nodes[i];

            const uint32_t newNodeID = scene->m_Nodes.size();
            Node& newNode = scene->m_Nodes.emplace_back();

            Matrix outLocalMatrix;
            cgltf_node_transform_local(&node, (cgltf_float*)&outLocalMatrix);

            verify(outLocalMatrix.Decompose(newNode.m_Scale, newNode.m_Rotation, newNode.m_Position));

            Matrix outWorldMatrix;
            cgltf_node_transform_world(&node, (cgltf_float*)&outWorldMatrix);

            Vector3 worldScale;
            Quaternion worldRotation;
            Vector3 worldPosition;
            verify(outWorldMatrix.Decompose(worldScale, worldRotation, worldPosition));

            AABB nodeAABB;
            newNode.m_AABB.Transform(nodeAABB, outWorldMatrix);

            AABB::CreateMerged(scene->m_AABB, scene->m_AABB, nodeAABB);

            Sphere nodeBS;
            newNode.m_BoundingSphere.Transform(nodeBS, outWorldMatrix);

            Sphere::CreateMerged(scene->m_BoundingSphere, scene->m_BoundingSphere, nodeBS);

            if (node.mesh)
            {
                for (const Primitive& primitive : m_SceneMeshPrimitives.at(cgltf_mesh_index(m_GLTFData, node.mesh)))
                {
                    const uint32_t primitiveID = scene->m_Primitives.size();

                    Primitive& newPrimitive = scene->m_Primitives.emplace_back();
                    newPrimitive.m_NodeID = newNodeID;
                    newPrimitive.m_MeshIdx = primitive.m_MeshIdx;
                    newPrimitive.m_Material = primitive.m_Material;

                    Mesh& primitiveMesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

                    AABB::CreateMerged(newNode.m_AABB, newNode.m_AABB, primitiveMesh.m_AABB);
                    Sphere::CreateMerged(newNode.m_BoundingSphere, newNode.m_BoundingSphere, primitiveMesh.m_BoundingSphere);

                    newNode.m_PrimitivesIDs.push_back(primitiveID);
                }
            }

            if (node.camera)
            {
                assert(node.camera->type == cgltf_camera_type_perspective);

                Scene::Camera& newCamera = scene->m_Cameras.emplace_back();

                newCamera.m_Name = node.name ? node.name : "Un-named Camera";
                newCamera.m_Orientation = worldRotation;
                newCamera.m_Position = worldPosition;
            }

            if (node.parent)
            {
                newNode.m_ParentNodeID = cgltf_node_index(m_GLTFData, node.parent);
            }

            for (uint32_t i = 0; i < node.children_count; ++i)
            {
                newNode.m_ChildrenNodeIDs.push_back(cgltf_node_index(m_GLTFData, node.children[i]));
            }
        }
    }
};

void LoadScene(std::string_view filePath)
{
    GLTFSceneLoader loader;
    loader.LoadScene(filePath);
}

#undef SCENE_LOAD_PROFILE
