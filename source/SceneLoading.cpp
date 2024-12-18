#define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#define CGLTF_IMPLEMENTATION
#include "extern/cgltf/cgltf.h"

#include "extern/basis_universal/transcoder/basisu_transcoder.h"
#include "extern/meshoptimizer/src/meshoptimizer.h"

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
#include "shaders/shared/MeshData.h"

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

    std::vector<RawVertexFormat> m_GlobalVertices;
    std::vector<Graphic::IndexBufferFormat_t> m_GlobalIndices;
    std::vector<MeshData> m_GlobalMeshData;
    std::vector<MaterialData> m_GlobalMaterialData;

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

				if (strcmp(m_GLTFData->extensions_used[i], "KHR_texture_basisu") == 0)
				{
                    basist::basisu_transcoder_init();
				}
            }
        }

        {
            SCENE_LOAD_PROFILE("Validate gltf data");

            cgltf_result result = cgltf_validate(m_GLTFData);
            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to validate '%s': [%s]", filePath.data(), EnumUtils::ToString(result));
                return;
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
        UploadGlobalBuffers();
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

        if (m_GLTFData->textures_count == 0)
        {
            return;
        }

        tf::Taskflow taskflow;

        m_SceneImages.resize(m_GLTFData->textures_count);
        for (uint32_t i = 0; i < m_GLTFData->textures_count; ++i)
        {
            taskflow.emplace([&, i]()
                {
                    const cgltf_texture& texture = m_GLTFData->textures[i];
                    const cgltf_image* image = texture.has_basisu ? texture.basisu_image : texture.image;
                    assert(image);

                    const char* debugName = image->name ? image->name : "Un-Named Image";

                    if (image->buffer_view)
                    {
                        debugName = !debugName ? image->buffer_view->name : debugName;
                        m_SceneImages[i].LoadFromMemory((std::byte*)image->buffer_view->buffer->data + image->buffer_view->offset, image->buffer_view->size, debugName);
                    }
                    else
                    {
                        assert(image->uri);

                        std::string filePath = (std::filesystem::path{ m_BaseFolderPath } / image->uri).string();
                        cgltf_decode_uri(filePath.data());

                        // ghetto hack to handle "MSFT_texture_dds" extension with 2 image URIs
                        for (uint32_t j = 0; j < texture.extensions_count; ++j)
                        {
                            if (strcmp(texture.extensions[j].name, "MSFT_texture_dds") == 0)
                            {
                                filePath = std::filesystem::path{ filePath }.replace_extension(".dds").string();
                            }
                        }

                        // ghetto hack to handle if "MSFT_texture_dds" extension was stripped away by MeshOptimizer
                        if (!std::filesystem::exists(filePath))
                        {
                            filePath = std::filesystem::path{ filePath }.replace_extension(".dds").string();
                        }

                        // final sanity check
                        assert(std::filesystem::exists(filePath));

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

                texture = m_SceneImages.at(cgltf_texture_index(m_GLTFData, textureView.texture));

                if (textureView.texture->sampler)
                {
                    texture.m_AddressMode = m_AddressModes.at(cgltf_sampler_index(m_GLTFData, textureView.texture->sampler));
                }

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
        m_GlobalMaterialData.resize(m_GLTFData->materials_count + 1); // +1 for default material

        for (uint32_t i = 0; i < m_GLTFData->materials_count; ++i)
        {
            Material& sceneMaterial = m_SceneMaterials[i];

            const cgltf_material& gltfMaterial = m_GLTFData->materials[i];
            const char* materialName = gltfMaterial.name ? gltfMaterial.name : "Un-Named Material";

            sceneMaterial.m_AlphaMode = (AlphaMode)gltfMaterial.alpha_mode;
            sceneMaterial.m_AlphaCutoff = gltfMaterial.alpha_cutoff;

            // todo: emissive
            if (Vector3{ gltfMaterial.emissive_factor }.LengthSquared() > kKindaSmallNumber)
            {
				LOG_DEBUG("Unhandled emissive_factor: [%s][%f, %f, %f]", materialName, gltfMaterial.emissive_factor[0], gltfMaterial.emissive_factor[1], gltfMaterial.emissive_factor[2]);
            }
            if (gltfMaterial.emissive_texture.texture)
            {
				LOG_DEBUG("Unhandled emissive_texture: [%s][%s]", materialName, gltfMaterial.emissive_texture.texture->name ? gltfMaterial.emissive_texture.texture->name : "");
            }
            if (gltfMaterial.has_emissive_strength)
            {
				LOG_DEBUG("Unhandled emissive_strength: [%s][%f]", materialName, gltfMaterial.emissive_strength.emissive_strength);
            }

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

                sceneMaterial.m_ConstDiffuse = Vector3{ &gltfMaterial.pbr_metallic_roughness.base_color_factor[0] };
                sceneMaterial.m_ConstMetallic = gltfMaterial.pbr_metallic_roughness.metallic_factor;
                sceneMaterial.m_ConstRoughness = gltfMaterial.pbr_metallic_roughness.roughness_factor;
            }
            else if (gltfMaterial.has_pbr_specular_glossiness)
            {
                if (gltfMaterial.pbr_specular_glossiness.diffuse_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseDiffuseTexture;
                    HandleTextureView(sceneMaterial.m_AlbedoTexture, gltfMaterial.pbr_specular_glossiness.diffuse_texture);
                }
                if (gltfMaterial.pbr_specular_glossiness.specular_glossiness_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                    HandleTextureView(sceneMaterial.m_MetallicRoughnessTexture, gltfMaterial.pbr_specular_glossiness.specular_glossiness_texture);
                }

                sceneMaterial.m_ConstDiffuse = Vector3{ &gltfMaterial.pbr_specular_glossiness.diffuse_factor[0] };
                sceneMaterial.m_ConstMetallic = std::max(std::max(gltfMaterial.pbr_specular_glossiness.specular_factor[0], gltfMaterial.pbr_specular_glossiness.specular_factor[1]), gltfMaterial.pbr_specular_glossiness.specular_factor[2]);
                sceneMaterial.m_ConstRoughness = 1.0f - gltfMaterial.pbr_specular_glossiness.glossiness_factor;
            }

            if (gltfMaterial.normal_texture.texture)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseNormalTexture;
                HandleTextureView(sceneMaterial.m_NormalTexture, gltfMaterial.normal_texture);
            }

            sceneMaterial.m_MaterialDataBufferIdx = i;

            MaterialData& materialData = m_GlobalMaterialData[i];
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
            materialData.m_AlphaCutoff = sceneMaterial.m_AlphaCutoff;

			LOG_DEBUG("New Material: [%s]", materialName);
        }

        MaterialData defaultMaterialData{};
        defaultMaterialData.m_ConstDiffuse = g_CommonResources.DefaultMaterial.m_ConstDiffuse;
        defaultMaterialData.m_ConstRoughness = g_CommonResources.DefaultMaterial.m_ConstRoughness;
        defaultMaterialData.m_ConstMetallic = g_CommonResources.DefaultMaterial.m_ConstMetallic;

        g_CommonResources.DefaultMaterial.m_MaterialDataBufferIdx = m_GlobalMaterialData.size();
        m_GlobalMaterialData.back() = defaultMaterialData;
    }

    void LoadMeshes()
    {
        SCENE_LOAD_PROFILE("Load Meshes");

        tf::Taskflow taskflow;

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        m_SceneMeshPrimitives.resize(m_GLTFData->meshes_count);
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[modelMeshIdx];

            m_SceneMeshPrimitives[modelMeshIdx].resize(mesh.primitives_count);

            for (uint32_t primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
            {
                // pre-create empty Mesh objects here due to MT init
                const uint32_t sceneMeshIdx = g_Graphic.m_Meshes.size();
                Mesh* newSceneMesh = &g_Graphic.m_Meshes.emplace_back();

                const uint32_t globalMeshDataIdx = m_GlobalMeshData.size();
                m_GlobalMeshData.emplace_back();

                const cgltf_primitive& gltfPrimitive = mesh.primitives[primitiveIdx];

                const cgltf_accessor* positionAccessor = cgltf_find_accessor(&gltfPrimitive, cgltf_attribute_type_position, 0);
                assert(positionAccessor);

                newSceneMesh->m_StartVertexLocation = totalVertices;
                newSceneMesh->m_StartIndexLocation = totalIndices;

                const uint32_t nbVertices = positionAccessor->count;

                totalVertices += nbVertices;
                totalIndices += gltfPrimitive.indices->count;

                taskflow.emplace([&, modelMeshIdx, primitiveIdx, sceneMeshIdx, globalMeshDataIdx, nbVertices]
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

                        std::vector<RawVertexFormat> vertices;
                        vertices.resize(nbVertices);

                        std::vector<float> scratchBuffer;
                        scratchBuffer.resize(nbVertices * 4);

                        for (size_t attrIdx = 0; attrIdx < gltfPrimitive.attributes_count; ++attrIdx)
                        {
                            const cgltf_attribute& attribute = gltfPrimitive.attributes[attrIdx];
                            const uint32_t nbFloats = cgltf_num_components(attribute.data->type);

                            if (attribute.type == cgltf_attribute_type_position)
                            {
                                verify(cgltf_accessor_unpack_floats(attribute.data, scratchBuffer.data(), attribute.data->count * nbFloats));
                                for (size_t j = 0; j < nbVertices; ++j)
                                {
                                    vertices[j].m_Position = Vector3{ &scratchBuffer[j * nbFloats] };
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_normal)
                            {
                                verify(cgltf_accessor_unpack_floats(attribute.data, scratchBuffer.data(), attribute.data->count * nbFloats));
								for (size_t j = 0; j < nbVertices; ++j)
								{
									Vector3 v{ &scratchBuffer[j * nbFloats] };

									assert(v.x >= (-1.0f - kKindaSmallNumber) && v.x <= (1.0f + kKindaBigNumber));
									assert(v.y >= (-1.0f - kKindaSmallNumber) && v.y <= (1.0f + kKindaBigNumber));
									assert(v.z >= (-1.0f - kKindaSmallNumber) && v.z <= (1.0f + kKindaBigNumber));

									v.x = std::clamp(v.x, -1.0f, 1.0f);
									v.y = std::clamp(v.y, -1.0f, 1.0f);
									v.z = std::clamp(v.z, -1.0f, 1.0f);

									// Normalize x, y, z from [-1, 1] to [0, 1]
									v = (v + Vector3::One) * 0.5f;

									// Scale to 10-bit integers (0-1023)
									const uint32_t xInt = (uint32_t)(v.x * 1023.0f);
									const uint32_t yInt = (uint32_t)(v.y * 1023.0f);
									const uint32_t zInt = (uint32_t)(v.z * 1023.0f);

									// Pack components into a uint32_t (10 bits each for x, y, z)
									vertices[j].m_PackedNormal = (xInt << 20) | (yInt << 10) | (zInt);
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) // only read the first UV set
                            {
                                verify(cgltf_accessor_unpack_floats(attribute.data, scratchBuffer.data(), attribute.data->count * nbFloats));

                                for (size_t j = 0; j < nbVertices; ++j)
                                {
									vertices[j].m_TexCoord = Half2{ &scratchBuffer[j * nbFloats] };
                                }
                            }
                            // TODO: cgltf_attribute_type_weights, cgltf_attribute_type_joints
                        }

                        Mesh* newSceneMesh = &g_Graphic.m_Meshes.at(sceneMeshIdx);
                        newSceneMesh->Initialize(vertices, indices, m_GLTFData->meshes[modelMeshIdx].name ? m_GLTFData->meshes[modelMeshIdx].name : "Un-named Mesh");
                        newSceneMesh->m_MeshDataBufferIdx = globalMeshDataIdx;

                        memcpy(&m_GlobalVertices[newSceneMesh->m_StartVertexLocation], vertices.data(), vertices.size() * sizeof(RawVertexFormat));
                        memcpy(&m_GlobalIndices[newSceneMesh->m_StartIndexLocation], indices.data(), indices.size() * sizeof(Graphic::IndexBufferFormat_t));

                        MeshData& meshData = m_GlobalMeshData[globalMeshDataIdx];
                        meshData.m_IndexCount = indices.size();
                        meshData.m_StartIndexLocation = newSceneMesh->m_StartIndexLocation;
                        meshData.m_StartVertexLocation = newSceneMesh->m_StartVertexLocation;
                        meshData.m_BoundingSphere = Vector4{ newSceneMesh->m_BoundingSphere.Center.x, newSceneMesh->m_BoundingSphere.Center.y, newSceneMesh->m_BoundingSphere.Center.z, newSceneMesh->m_BoundingSphere.Radius };
                        meshData.m_AABBCenter = newSceneMesh->m_AABB.Center;
                        meshData.m_AABBExtents = newSceneMesh->m_AABB.Extents;

                        Primitive& primitive = m_SceneMeshPrimitives[modelMeshIdx][primitiveIdx];
                        if (gltfPrimitive.material)
                        {
                            primitive.m_Material = m_SceneMaterials.at(cgltf_material_index(m_GLTFData, gltfPrimitive.material));
                        }
                        else
                        {
                            primitive.m_Material = g_CommonResources.DefaultMaterial;
                        }
                        primitive.m_MeshIdx = sceneMeshIdx;
                    });
            }
        }

        m_GlobalVertices.resize(totalVertices);
        m_GlobalIndices.resize(totalIndices);

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

                    switch (primitive.m_Material.m_AlphaMode)
                    {
                    case AlphaMode::Opaque:
                        scene->m_OpaquePrimitiveIDs.push_back(primitiveID);
                        break;
                    case AlphaMode::Mask:
                        scene->m_AlphaMaskPrimitiveIDs.push_back(primitiveID);
                        break;
                    case AlphaMode::Blend:
                        scene->m_TransparentPrimitiveIDs.push_back(primitiveID);
                        break;
                    default:
                        assert(0);
                    }
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

            if (node.light)
            {
                if (node.light->type == cgltf_light_type_directional)
                {
                    scene->m_DirLightVec = -outWorldMatrix.Forward();

                    // Ensure the vector has valid length
                    assert(scene->m_DirLightVec.LengthSquared() <= (1 + kKindaSmallNumber));

                    // Step 1: Calculate m_SunInclination (phi)
                    scene->m_SunInclination = std::asin(scene->m_DirLightVec.y);  // Asin returns radians
                    scene->m_SunInclination = ConvertToDegrees(scene->m_SunInclination); // Convert to degrees

                    // Step 2: Calculate m_SunOrientation (theta)
                    scene->m_SunOrientation = std::atan2(scene->m_DirLightVec.z, scene->m_DirLightVec.x); // Atan2 returns radians
                    scene->m_SunOrientation = ConvertToDegrees(scene->m_SunOrientation); // Convert to degrees
                }
            }

            if (node.parent)
            {
                newNode.m_ParentNodeID = cgltf_node_index(m_GLTFData, node.parent);
            }

            for (uint32_t i = 0; i < node.children_count; ++i)
            {
                newNode.m_ChildrenNodeIDs.push_back(cgltf_node_index(m_GLTFData, node.children[i]));
            }

			//LOG_DEBUG("New Node: [%s]", node.name ? node.name : "Un-named Node");
        }
    }

    void UploadGlobalBuffers()
    {
        SCENE_LOAD_PROFILE("Upload Global Buffers");

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalVertices.size() * sizeof(RawVertexFormat);
            desc.structStride = sizeof(RawVertexFormat);
            desc.debugName = "Global Vertex Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalVertexBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalIndices.size() * sizeof(Graphic::IndexBufferFormat_t);
            desc.debugName = "Global Index Buffer";
            desc.format = Graphic::kIndexBufferFormat;
            desc.isIndexBuffer = true;
            desc.initialState = nvrhi::ResourceStates::IndexBuffer;
            g_Graphic.m_GlobalIndexBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalMeshData.size() * sizeof(MeshData);
            desc.structStride = sizeof(MeshData);
            desc.debugName = "Global Mesh Data Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMeshDataBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalMaterialData.size() * sizeof(MaterialData);
            desc.structStride = sizeof(MaterialData);
            desc.debugName = "Global Material Data Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMaterialDataBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        LOG_DEBUG("Global vertices = [%d] vertices, [%f] MB", m_GlobalVertices.size(), BYTES_TO_MB(m_GlobalVertices.size() * sizeof(RawVertexFormat)));
        LOG_DEBUG("Global indices = [%d] indices, [%f] MB", m_GlobalIndices.size(), BYTES_TO_MB(m_GlobalIndices.size() * sizeof(Graphic::IndexBufferFormat_t)));
        LOG_DEBUG("Global mesh data = [%d] entries, [%f] MB", m_GlobalMeshData.size(), BYTES_TO_MB(m_GlobalMeshData.size() * sizeof(MeshData)));
        LOG_DEBUG("Global material data = [%d] entries, [%f] MB", m_GlobalMaterialData.size(), BYTES_TO_MB(m_GlobalMaterialData.size() * sizeof(MaterialData)));

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Upload Global Buffers");

        commandList->writeBuffer(g_Graphic.m_GlobalVertexBuffer, m_GlobalVertices.data(), m_GlobalVertices.size() * sizeof(RawVertexFormat));
        commandList->writeBuffer(g_Graphic.m_GlobalIndexBuffer, m_GlobalIndices.data(), m_GlobalIndices.size() * sizeof(Graphic::IndexBufferFormat_t));
        commandList->writeBuffer(g_Graphic.m_GlobalMeshDataBuffer, m_GlobalMeshData.data(), m_GlobalMeshData.size() * sizeof(MeshData));
        commandList->writeBuffer(g_Graphic.m_GlobalMaterialDataBuffer, m_GlobalMaterialData.data(), m_GlobalMaterialData.size() * sizeof(MaterialData));
    }
};

void LoadScene(std::string_view filePath)
{
    GLTFSceneLoader loader;
    loader.LoadScene(filePath);
}

#undef SCENE_LOAD_PROFILE
