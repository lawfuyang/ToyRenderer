#define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#define CGLTF_IMPLEMENTATION
#include "extern/cgltf/cgltf.h"

#include "extern/meshoptimizer/src/meshoptimizer.h"

#include "CommonResources.h"
#include "Engine.h"
#include "DescriptorTableManager.h"
#include "Graphic.h"
#include "Scene.h"
#include "SmallVector.h"
#include "Utilities.h"
#include "Visual.h"

#include "shaders/ShaderInterop.h"

CommandLineOption<std::string> g_SceneToLoad{ "scene", "" };
CommandLineOption<float> g_CustomSceneScale{ "customscenescale", 0.0f };

#define SCENE_LOAD_PROFILE(x) \
    PROFILE_SCOPED(x);        \
    SCOPED_TIMER_NAMED(x);

struct GLTFSceneLoader
{
    std::string m_FileName;
    std::string m_BaseFolderPath;
    std::string m_CachedDataFilePath;

    bool m_bHasValidCachedData = true;
    bool m_bIsDefaultScene = false;

    cgltf_data* m_GLTFData = nullptr;

    std::vector<nvrhi::SamplerAddressMode> m_AddressModes;
    std::vector<std::vector<Primitive>> m_SceneMeshPrimitives;
    std::vector<Material> m_SceneMaterials;

    std::vector<RawVertexFormat> m_GlobalVertices;
    std::vector<GraphicConstants::IndexBufferFormat_t> m_GlobalIndices;
    std::vector<MeshData> m_GlobalMeshData;
    std::vector<MaterialData> m_GlobalMaterialData;

    struct GlobalMeshletDataEntry
    {
        uint32_t m_SceneMeshIdx;
        std::vector<uint32_t> m_VertexIdxOffsets;
        std::vector<uint32_t> m_Indices;
        std::vector<MeshletData> m_Meshlets;
    };
	std::vector<GlobalMeshletDataEntry> m_MeshletDataEntries;

    std::vector<uint32_t> m_GlobalMeshletVertexIdxOffsets;
    std::vector<uint32_t> m_GlobalMeshletIndices;
    std::vector<MeshletData> m_GlobalMeshletDatas;

    struct CachedData
    {
        static const uint32_t kCurrentVersion = 2; // increment this if the cached mesh data format changes

        struct Header
        {
            uint32_t m_Version = kCurrentVersion;
            uint32_t m_NumVertices = 0;
            uint32_t m_NumIndices = 0;
            uint32_t m_NumMeshes = 0;
            uint32_t m_NumMeshletVertexIdxOffsets = 0;
            uint32_t m_NumMeshletIndices = 0;
            uint32_t m_NumMeshletDatas = 0;
        };

        struct MeshSpecificData
        {
            uint32_t m_NumIndices = 0;
            uint32_t m_NumVertices = 0;
            AABB m_AABB = { Vector3::Zero, Vector3::Zero };
        };
    };

    void PreloadScene()
    {
        SCENE_LOAD_PROFILE("Preload Scene");

        std::string_view sceneToLoad = g_SceneToLoad.Get();

        if (sceneToLoad.empty())
        {
            const char* kDefaultScene = "cornell.gltf";
            static std::string defaultScenePath = (std::filesystem::path{ GetRootDirectory() } / "resources" / kDefaultScene).string();
            sceneToLoad = defaultScenePath;
            m_bIsDefaultScene = true;
        }

        m_FileName = std::filesystem::path{ sceneToLoad }.stem().string();
        m_BaseFolderPath = std::filesystem::path{ sceneToLoad }.parent_path().string();
        m_CachedDataFilePath = (std::filesystem::path{ m_BaseFolderPath } / (m_FileName + "_CachedData.bin")).string();

        m_bHasValidCachedData = std::filesystem::exists(m_CachedDataFilePath) && !m_bIsDefaultScene;
        if (m_bHasValidCachedData)
        {
            ScopedFile cachedDataFile{ m_CachedDataFilePath, "rb" };

            CachedData::Header header;
            size_t objectsRead = fread(&header, sizeof(header), 1, cachedDataFile);
            assert(objectsRead == 1);

            m_bHasValidCachedData = (header.m_Version == CachedData::kCurrentVersion);
        }

        cgltf_options options{};

        {
            SCENE_LOAD_PROFILE("Load gltf file");

            cgltf_result result = cgltf_parse_file(&options, sceneToLoad.data(), &m_GLTFData);

            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to load '%s': [%s]", sceneToLoad.data(), EnumUtils::ToString(result));
                assert(0);
            }
            LOG_DEBUG("GLTF - Loaded '%s'", sceneToLoad.data());

            LOG_DEBUG("Extensions used: ");
            for (uint32_t i = 0; i < m_GLTFData->extensions_used_count; ++i)
            {
                LOG_DEBUG("\t %s", m_GLTFData->extensions_used[i]);

                static const char* kUnsupportedExtensions[]
                {
                    "EXT_mesh_gpu_instancing", // mesh_gpu_instancing merely reduces the nb of nodes to read, but breaks scene hierarchy and i'm lazy to investigate & fix
                    "KHR_texture_transform", // don't bother with texture transform
                    "KHR_texture_basisu" // No KTX textures. Just support DDS only for now
                };

                for (const char* ext : kUnsupportedExtensions)
                {
                    assert(strcmp(ext, m_GLTFData->extensions_used[i]));
                }
            }
        }

        {
            SCENE_LOAD_PROFILE("Validate gltf data");

            cgltf_result result = cgltf_validate(m_GLTFData);
            if (result != cgltf_result_success)
            {
                LOG_DEBUG("GLTF - Failed to validate '%s': [%s]", sceneToLoad.data(), EnumUtils::ToString(result));
                assert(0);
            }
        }

        if (!m_bHasValidCachedData)
        {
            {
                SCENE_LOAD_PROFILE("Load gltf buffers");

                cgltf_result result = cgltf_load_buffers(&options, m_GLTFData, sceneToLoad.data());
                if (result != cgltf_result_success)
                {
                    LOG_DEBUG("GLTF - Failed to load buffers '%s': [%s]", sceneToLoad.data(), EnumUtils::ToString(result));
                    assert(0);
                }
            }

            {
                SCENE_LOAD_PROFILE("Decompress buffers");

                const cgltf_result result = decompressMeshopt(m_GLTFData);
                assert(result == cgltf_result_success);
            }
        }
        else
        {
            LoadAnimations();
            LoadCachedData();
        }
    }

    void LoadScene()
    {
        SCENE_LOAD_PROFILE("Load Scene");

        assert(m_GLTFData);
        ON_EXIT_SCOPE_LAMBDA([this] { cgltf_free(m_GLTFData); });

        LoadSamplers();
        LoadImages();
        LoadMaterials();

        if (m_bHasValidCachedData)
        {
            PrePopulateSceneMeshPrimitives();
        }
        else
        {
            LoadMeshes();

            assert(m_MeshletDataEntries.size() == m_GlobalMeshData.size());

            // flatten per-primitive meshlet buffers into global buffers
            for (uint32_t i = 0; i < m_MeshletDataEntries.size(); ++i)
            {
                GlobalMeshletDataEntry& meshletDataEntry = m_MeshletDataEntries.at(i);
                Mesh& sceneMesh = g_Graphic.m_Meshes.at(meshletDataEntry.m_SceneMeshIdx);

                for (MeshletData& meshletData : meshletDataEntry.m_Meshlets)
                {
                    meshletData.m_MeshletVertexIDsBufferIdx += m_GlobalMeshletVertexIdxOffsets.size();
                    meshletData.m_MeshletIndexIDsBufferIdx += m_GlobalMeshletIndices.size();
                }

                for (uint32_t lodIdx = 0; lodIdx < kMaxNumMeshLODs; ++lodIdx)
                {
                    MeshLODData& meshLODData = m_GlobalMeshData.at(i).m_MeshLODDatas[lodIdx];
                    meshLODData.m_MeshletDataBufferIdx += m_GlobalMeshletDatas.size();
                }

                m_GlobalMeshletVertexIdxOffsets.insert(m_GlobalMeshletVertexIdxOffsets.end(), meshletDataEntry.m_VertexIdxOffsets.begin(), meshletDataEntry.m_VertexIdxOffsets.end());
                m_GlobalMeshletIndices.insert(m_GlobalMeshletIndices.end(), meshletDataEntry.m_Indices.begin(), meshletDataEntry.m_Indices.end());
                m_GlobalMeshletDatas.insert(m_GlobalMeshletDatas.end(), meshletDataEntry.m_Meshlets.begin(), meshletDataEntry.m_Meshlets.end());
            }

            nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
            SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "UploadGlobalMeshBuffers");

            UploadGlobalMeshBuffers(commandList);
        }

        LoadAnimations();
        LoadNodes();
        UploadGlobalMaterialBuffer();
        WriteCachedData();
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

            auto GLtoTextureAddressMode = [](cgltf_wrap_mode wrapMode)
                {
                    switch (wrapMode)
                    {
                    case cgltf_wrap_mode_clamp_to_edge: return nvrhi::SamplerAddressMode::Clamp;
                    case cgltf_wrap_mode_mirrored_repeat: return nvrhi::SamplerAddressMode::Mirror;
                    case cgltf_wrap_mode_repeat: return nvrhi::SamplerAddressMode::Wrap;
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

        g_Graphic.m_Textures.resize(m_GLTFData->textures_count);
        for (uint32_t i = 0; i < m_GLTFData->textures_count; ++i)
        {
            taskflow.emplace([&, i]()
                {
                    const cgltf_texture& texture = m_GLTFData->textures[i];
                    const cgltf_image* image = texture.image;
                    assert(image);
                    assert(!image->buffer_view); // dont support images in buffer views
                    assert(image->uri);

                    std::string filePath = (std::filesystem::path{m_BaseFolderPath} / image->uri).string();
                    cgltf_decode_uri(filePath.data());

                    // force DDS format for all textures
                    filePath = std::filesystem::path{filePath}.replace_extension(".dds").string();

                    g_Graphic.m_Textures[i].LoadFromFile(filePath);
                });
        }

        g_Engine.m_Executor->corun(taskflow);
    }

    void LoadMaterials()
    {
        SCENE_LOAD_PROFILE("Load Materials");

        auto HandleTextureView = [&](Material::TextureView& sceneTextureView, const cgltf_texture_view& textureView)
            {
                const cgltf_image* image = textureView.texture->image;
                assert(image);

                sceneTextureView.m_TextureIdx = cgltf_texture_index(m_GLTFData, textureView.texture);

                if (textureView.texture->sampler)
                {
                    sceneTextureView.m_AddressMode = m_AddressModes.at(cgltf_sampler_index(m_GLTFData, textureView.texture->sampler));
                    assert(sceneTextureView.m_AddressMode == nvrhi::SamplerAddressMode::Clamp || sceneTextureView.m_AddressMode == nvrhi::SamplerAddressMode::Wrap);
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

            if (Vector3{ gltfMaterial.emissive_factor }.LengthSquared() > kKindaSmallNumber)
            {
				sceneMaterial.m_ConstEmissive = Vector3{ gltfMaterial.emissive_factor };

                if (gltfMaterial.has_emissive_strength)
                {
                    sceneMaterial.m_ConstEmissive *= gltfMaterial.emissive_strength.emissive_strength;
                }
            }
            if (gltfMaterial.emissive_texture.texture)
            {
				sceneMaterial.m_MaterialFlags |= MaterialFlag_UseEmissiveTexture;
				HandleTextureView(sceneMaterial.m_Emissive, gltfMaterial.emissive_texture);
			}

            if (gltfMaterial.has_pbr_specular_glossiness)
            {
                if (gltfMaterial.pbr_specular_glossiness.diffuse_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseDiffuseTexture;
                    HandleTextureView(sceneMaterial.m_Albedo, gltfMaterial.pbr_specular_glossiness.diffuse_texture);
                }
                if (gltfMaterial.pbr_specular_glossiness.specular_glossiness_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                    HandleTextureView(sceneMaterial.m_MetallicRoughness, gltfMaterial.pbr_specular_glossiness.specular_glossiness_texture);
                }

                sceneMaterial.m_ConstAlbedo = Vector4{ &gltfMaterial.pbr_specular_glossiness.diffuse_factor[0] };
                sceneMaterial.m_ConstMetallic = std::max(std::max(gltfMaterial.pbr_specular_glossiness.specular_factor[0], gltfMaterial.pbr_specular_glossiness.specular_factor[1]), gltfMaterial.pbr_specular_glossiness.specular_factor[2]);
                sceneMaterial.m_ConstRoughness = 1.0f - gltfMaterial.pbr_specular_glossiness.glossiness_factor;
            }
            else if (gltfMaterial.has_pbr_metallic_roughness)
            {
                if (gltfMaterial.pbr_metallic_roughness.base_color_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseDiffuseTexture;
                    HandleTextureView(sceneMaterial.m_Albedo, gltfMaterial.pbr_metallic_roughness.base_color_texture);
                }
                if (gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture.texture)
                {
                    sceneMaterial.m_MaterialFlags |= MaterialFlag_UseMetallicRoughnessTexture;
                    HandleTextureView(sceneMaterial.m_MetallicRoughness, gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture);
                }

                sceneMaterial.m_ConstAlbedo = Vector4{ &gltfMaterial.pbr_metallic_roughness.base_color_factor[0] };
                sceneMaterial.m_ConstMetallic = gltfMaterial.pbr_metallic_roughness.metallic_factor;
                sceneMaterial.m_ConstRoughness = gltfMaterial.pbr_metallic_roughness.roughness_factor;
            }
            else
            {
                sceneMaterial.m_ConstAlbedo = Vector4::One;
                sceneMaterial.m_ConstMetallic = 0.0f;
                sceneMaterial.m_ConstRoughness = 1.0f;
            }

            if (gltfMaterial.has_transmission)
            {
                // forcefully tag this as 'transparent' for the Forward Renderer
                sceneMaterial.m_AlphaMode = AlphaMode::Blend;

				// sanity check that the alpha channel is not used
                // we'll use the .w channel of material albedo as alpha for transmission. Pretty sure it's not physically correct, but i don't care
				assert(sceneMaterial.m_ConstAlbedo.w == 1.0f);
                sceneMaterial.m_ConstAlbedo.w = 1.0f - gltfMaterial.transmission.transmission_factor;

				// TODO: support transmission texture
                assert(gltfMaterial.transmission.transmission_texture.texture == nullptr);
            }

            if (gltfMaterial.double_sided && sceneMaterial.m_AlphaMode == AlphaMode::Opaque)
            {
                // forcefully tag this as 'Mask', as double-sided rendering is enabled for this mode
                sceneMaterial.m_AlphaMode = AlphaMode::Mask;
            }

            if (gltfMaterial.normal_texture.texture)
            {
                sceneMaterial.m_MaterialFlags |= MaterialFlag_UseNormalTexture;
                HandleTextureView(sceneMaterial.m_Normal, gltfMaterial.normal_texture);
            }

            sceneMaterial.m_MaterialDataBufferIdx = i;

            auto GetPackedSamplerAndDescriptorIndices = [this](uint32_t& textureSamplerAndDescriptorIndex, uint32_t& feedbackAndMinMiptextureDescriptorIndex, const Material::TextureView& sceneTextureView)
            {
                textureSamplerAndDescriptorIndex = 0xFFFFFFFF; // invalid value
                feedbackAndMinMiptextureDescriptorIndex = 0xFFFFFFFF; // invalid value
                if (!sceneTextureView.IsValid())
                {
                    return; // no texture
                }

                Texture& tex = g_Graphic.m_Textures.at(sceneTextureView.m_TextureIdx);

                uint32_t feedbackSRVIndexInHeap = UINT16_MAX;
                uint32_t minMipSRVIndexInHeap = UINT16_MAX;
                if (tex.m_PackedMipDesc.numStandardMips != 0)
                {
                    assert(tex.m_SamplerFeedbackTextureHandle);
                    assert(tex.m_MinMipTextureHandle);

                    feedbackSRVIndexInHeap = g_Graphic.GetIndexInHeap(tex.m_SamplerFeedbackIndexInTable);
                    assert(feedbackSRVIndexInHeap < UINT16_MAX);

                    minMipSRVIndexInHeap = g_Graphic.GetIndexInHeap(tex.m_MinMipIndexInTable);
                    assert(minMipSRVIndexInHeap < UINT16_MAX);
                }

                // need to fit srv index in bottom 30 bits of the packed value
                const uint32_t textureSRVIndexInHeap = g_Graphic.GetIndexInHeap(tex.m_SRVIndexInTable);
                assert(textureSRVIndexInHeap < (1u << 31));

                const uint32_t samplerVal = sceneTextureView.m_AddressMode == nvrhi::SamplerAddressMode::Wrap ? 1 : 0;

                textureSamplerAndDescriptorIndex = textureSRVIndexInHeap | (samplerVal << 31);
                feedbackAndMinMiptextureDescriptorIndex = (feedbackSRVIndexInHeap & 0xFFFF) | (minMipSRVIndexInHeap << 16);
            };

            MaterialData& materialData = m_GlobalMaterialData[i];
            materialData.m_ConstAlbedo = sceneMaterial.m_ConstAlbedo;
			materialData.m_ConstEmissive = sceneMaterial.m_ConstEmissive;
            materialData.m_MaterialFlags = sceneMaterial.m_MaterialFlags;
            GetPackedSamplerAndDescriptorIndices(materialData.m_AlbedoTextureSamplerAndDescriptorIndex, materialData.m_AlbedoFeedbackAndMinMapTexturesDescriptorIndex, sceneMaterial.m_Albedo);
            GetPackedSamplerAndDescriptorIndices(materialData.m_NormalTextureSamplerAndDescriptorIndex, materialData.m_NormalFeedbackAndMinMapTexturesDescriptorIndex, sceneMaterial.m_Normal);
            GetPackedSamplerAndDescriptorIndices(materialData.m_MetallicRoughnessTextureSamplerAndDescriptorIndex, materialData.m_MetallicRoughnessFeedbackAndMinMapTexturesDescriptorIndex, sceneMaterial.m_MetallicRoughness);
            GetPackedSamplerAndDescriptorIndices(materialData.m_EmissiveTextureSamplerAndDescriptorIndex, materialData.m_EmissiveFeedbackAndMinMapTexturesDescriptorIndex, sceneMaterial.m_Emissive);
            materialData.m_ConstRoughness = sceneMaterial.m_ConstRoughness;
            materialData.m_ConstMetallic = sceneMaterial.m_ConstMetallic;
            materialData.m_AlphaCutoff = sceneMaterial.m_AlphaCutoff;

			LOG_DEBUG("New Material: [%s]", materialName);
        }

        MaterialData defaultMaterialData{};
        defaultMaterialData.m_ConstAlbedo = g_CommonResources.DefaultMaterial.m_ConstAlbedo;
        defaultMaterialData.m_ConstRoughness = g_CommonResources.DefaultMaterial.m_ConstRoughness;

        g_CommonResources.DefaultMaterial.m_MaterialDataBufferIdx = m_GlobalMaterialData.size();
        m_GlobalMaterialData.back() = defaultMaterialData;
    }

    void LoadMeshes()
    {
        SCENE_LOAD_PROFILE("Load Meshes");

        PrePopulateSceneMeshPrimitives();

        tf::Taskflow taskflow;

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            const cgltf_mesh& gltfMesh = m_GLTFData->meshes[modelMeshIdx];

            for (uint32_t primitiveIdx = 0; primitiveIdx < gltfMesh.primitives_count; ++primitiveIdx)
            {
                // pre-create empty Mesh objects here due to MT init
                const uint32_t sceneMeshIdx = g_Graphic.m_Meshes.size();
                Mesh* newSceneMesh = &g_Graphic.m_Meshes.emplace_back();
                m_GlobalMeshData.emplace_back();

				const uint32_t meshletDataEntryIdx = m_MeshletDataEntries.size();
                m_MeshletDataEntries.emplace_back();
                m_MeshletDataEntries.back().m_SceneMeshIdx = sceneMeshIdx;

                const cgltf_primitive& gltfPrimitive = gltfMesh.primitives[primitiveIdx];

                const cgltf_accessor* positionAccessor = cgltf_find_accessor(&gltfPrimitive, cgltf_attribute_type_position, 0);
                assert(positionAccessor);

                const uint32_t globalVertexBufferIdxOffset = totalVertices;
                const uint32_t globalIndexBufferIdxOffset = totalIndices;

                const uint32_t nbVertices = positionAccessor->count;

                totalVertices += nbVertices;
                totalIndices += gltfPrimitive.indices->count;

                taskflow.emplace([&, modelMeshIdx, primitiveIdx, sceneMeshIdx, meshletDataEntryIdx, globalVertexBufferIdxOffset, globalIndexBufferIdxOffset, nbVertices]
                    {
                        PROFILE_SCOPED("Load Primitive");

                        const cgltf_primitive& gltfPrimitive = gltfMesh.primitives[primitiveIdx];
                        assert(gltfPrimitive.type == cgltf_primitive_type_triangles);

                        std::vector<GraphicConstants::IndexBufferFormat_t> indices;
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
                                    vertices[j].m_PackedNormal = Mesh::PackNormal(Vector3{ &scratchBuffer[j * nbFloats] });
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
                        newSceneMesh->Initialize(
                            vertices,
                            indices,
                            globalVertexBufferIdxOffset,
                            globalIndexBufferIdxOffset,
                            m_MeshletDataEntries[meshletDataEntryIdx].m_VertexIdxOffsets,
                            m_MeshletDataEntries[meshletDataEntryIdx].m_Indices,
                            m_MeshletDataEntries[meshletDataEntryIdx].m_Meshlets,
                            m_GLTFData->meshes[modelMeshIdx].name ? m_GLTFData->meshes[modelMeshIdx].name : "Un-named Mesh");

                        newSceneMesh->m_MeshDataBufferIdx = sceneMeshIdx;

                        memcpy(&m_GlobalVertices[globalVertexBufferIdxOffset], vertices.data(), vertices.size() * sizeof(RawVertexFormat));
                        memcpy(&m_GlobalIndices[globalIndexBufferIdxOffset], indices.data(), indices.size() * sizeof(GraphicConstants::IndexBufferFormat_t));

                        MeshData& meshData = m_GlobalMeshData[sceneMeshIdx];
                        meshData.m_BoundingSphere = Vector4{ newSceneMesh->m_BoundingSphere.Center.x, newSceneMesh->m_BoundingSphere.Center.y, newSceneMesh->m_BoundingSphere.Center.z, newSceneMesh->m_BoundingSphere.Radius };
                        meshData.m_NumLODs = newSceneMesh->m_NumLODs;
                        meshData.m_GlobalVertexBufferIdx = globalVertexBufferIdxOffset;
                        meshData.m_GlobalIndexBufferIdx = globalIndexBufferIdxOffset;
                        
                        for (uint32_t meshLODIdx = 0; meshLODIdx < kMaxNumMeshLODs; ++meshLODIdx)
                        {
                            MeshLODData& meshLODData = meshData.m_MeshLODDatas[meshLODIdx];
                            MeshLOD& meshLOD = newSceneMesh->m_LODs[meshLODIdx];

                            meshLODData.m_MeshletDataBufferIdx = meshLOD.m_MeshletDataBufferIdx;
                            meshLODData.m_NumMeshlets = meshLOD.m_NumMeshlets;
                            meshLODData.m_Error = meshLOD.m_Error;
                        }
                    });
            }
        }

        m_GlobalVertices.resize(totalVertices);
        m_GlobalIndices.resize(totalIndices);

        g_Engine.m_Executor->corun(taskflow);
    }

    void PrePopulateSceneMeshPrimitives()
    {
        SCENE_LOAD_PROFILE("Pre-populate Scene Mesh Primitives");

        m_SceneMeshPrimitives.resize(m_GLTFData->meshes_count);

        uint32_t sceneMeshIdx = 0;
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            const cgltf_mesh& mesh = m_GLTFData->meshes[modelMeshIdx];
            m_SceneMeshPrimitives[modelMeshIdx].resize(mesh.primitives_count);

            for (uint32_t primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
            {
                const cgltf_primitive& gltfPrimitive = mesh.primitives[primitiveIdx];

                Primitive& primitive = m_SceneMeshPrimitives[modelMeshIdx][primitiveIdx];
                if (gltfPrimitive.material)
                {
                    primitive.m_Material = m_SceneMaterials.at(cgltf_material_index(m_GLTFData, gltfPrimitive.material));
                }
                else
                {
                    primitive.m_Material = g_CommonResources.DefaultMaterial;
                }
                primitive.m_MeshIdx = sceneMeshIdx++;
            }
        }
    }

    void LoadCachedData()
    {
        SCENE_LOAD_PROFILE("Load Cached Data");

        uint32_t totalMeshes = 0;
        for (uint32_t modelMeshIdx = 0; modelMeshIdx < m_GLTFData->meshes_count; ++modelMeshIdx)
        {
            totalMeshes += m_GLTFData->meshes[modelMeshIdx].primitives_count;
        }
        g_Graphic.m_Meshes.resize(totalMeshes);

        ScopedFile cachedDataFile{ m_CachedDataFilePath, "rb" };

        CachedData::Header header;
        size_t objectsRead = fread(&header, sizeof(header), 1, cachedDataFile);
        assert(objectsRead == 1);

        assert(totalMeshes == header.m_NumMeshes);

        m_GlobalVertices.resize(header.m_NumVertices);
        m_GlobalIndices.resize(header.m_NumIndices);
        m_GlobalMeshData.resize(header.m_NumMeshes);
        m_GlobalMeshletVertexIdxOffsets.resize(header.m_NumMeshletVertexIdxOffsets);
        m_GlobalMeshletIndices.resize(header.m_NumMeshletIndices);
        m_GlobalMeshletDatas.resize(header.m_NumMeshletDatas);

        std::vector<CachedData::MeshSpecificData> meshSpecificDataArray;
        meshSpecificDataArray.resize(header.m_NumMeshes);

        objectsRead = fread(m_GlobalVertices.data(), sizeof(RawVertexFormat), header.m_NumVertices, cachedDataFile);
        assert(objectsRead == header.m_NumVertices);

        objectsRead = fread(m_GlobalIndices.data(), sizeof(GraphicConstants::IndexBufferFormat_t), header.m_NumIndices, cachedDataFile);
        assert(objectsRead == header.m_NumIndices);

        objectsRead = fread(m_GlobalMeshData.data(), sizeof(MeshData), header.m_NumMeshes, cachedDataFile);
        assert(objectsRead == header.m_NumMeshes);

        objectsRead = fread(m_GlobalMeshletVertexIdxOffsets.data(), sizeof(uint32_t), header.m_NumMeshletVertexIdxOffsets, cachedDataFile);
        assert(objectsRead == header.m_NumMeshletVertexIdxOffsets);

        objectsRead = fread(m_GlobalMeshletIndices.data(), sizeof(uint32_t), header.m_NumMeshletIndices, cachedDataFile);
        assert(objectsRead == header.m_NumMeshletIndices);

        objectsRead = fread(m_GlobalMeshletDatas.data(), sizeof(MeshletData), header.m_NumMeshletDatas, cachedDataFile);
        assert(objectsRead == header.m_NumMeshletDatas);

        objectsRead = fread(meshSpecificDataArray.data(), sizeof(CachedData::MeshSpecificData), header.m_NumMeshes, cachedDataFile);
        assert(objectsRead == header.m_NumMeshes);

        for (uint32_t i = 0; i < totalMeshes; ++i)
        {
            Mesh& mesh = g_Graphic.m_Meshes[i];

            mesh.m_GlobalVertexBufferIdx = m_GlobalMeshData[i].m_GlobalVertexBufferIdx;
            mesh.m_GlobalIndexBufferIdx = m_GlobalMeshData[i].m_GlobalIndexBufferIdx;
            mesh.m_NumIndices = meshSpecificDataArray[i].m_NumIndices;
            mesh.m_NumVertices = meshSpecificDataArray[i].m_NumVertices;

            for (uint32_t meshLODIdx = 0; meshLODIdx < m_GlobalMeshData[i].m_NumLODs; ++meshLODIdx)
            {
                MeshLOD& meshLOD = mesh.m_LODs[meshLODIdx];
                MeshLODData& meshLODData = m_GlobalMeshData[i].m_MeshLODDatas[meshLODIdx];

                meshLOD.m_MeshletDataBufferIdx = meshLODData.m_MeshletDataBufferIdx;
                meshLOD.m_NumMeshlets = meshLODData.m_NumMeshlets;
                meshLOD.m_Error = meshLODData.m_Error;
            }

            mesh.m_NumLODs = m_GlobalMeshData[i].m_NumLODs;
            mesh.m_MeshDataBufferIdx = i;

            mesh.m_BoundingSphere.Center = Vector3{ m_GlobalMeshData[i].m_BoundingSphere.x, m_GlobalMeshData[i].m_BoundingSphere.y, m_GlobalMeshData[i].m_BoundingSphere.z };
            mesh.m_BoundingSphere.Radius = m_GlobalMeshData[i].m_BoundingSphere.w;
            mesh.m_AABB = meshSpecificDataArray[i].m_AABB;
        }

        if (m_GLTFData->animations_count > 0)
        {
            assert(!g_Scene->m_Animations.empty());

            for (Animation& animation : g_Scene->m_Animations)
            {
                objectsRead = fread(&animation.m_TimeStart, sizeof(float), 1, cachedDataFile);
                assert(objectsRead == 1);

                objectsRead = fread(&animation.m_TimeEnd, sizeof(float), 1, cachedDataFile);
                assert(objectsRead == 1);

                assert(!animation.m_Channels.empty());

                for (Animation::Channel& channel : animation.m_Channels)
                {
                    assert(!channel.m_KeyFrames.empty());
                    assert(!channel.m_Data.empty());

                    objectsRead = fread(channel.m_KeyFrames.data(), sizeof(float), channel.m_KeyFrames.size(), cachedDataFile);
                    assert(objectsRead == channel.m_KeyFrames.size());

                    objectsRead = fread(channel.m_Data.data(), sizeof(Vector4), channel.m_Data.size(), cachedDataFile);   
                    assert(objectsRead == channel.m_Data.size());
                }
            }
        }
    }

    void LoadNodes()
    {
        SCENE_LOAD_PROFILE("Load Nodes");

        if (const float customSceneScale = g_CustomSceneScale.Get();
            g_CustomSceneScale.Get() > 0.0f)
        {
            for (uint32_t i = 0; i < m_GLTFData->nodes_count; ++i)
            {
                cgltf_node& node = m_GLTFData->nodes[i];
                node.scale[0] *= customSceneScale;
                node.scale[1] *= customSceneScale;
                node.scale[2] *= customSceneScale;

                node.translation[0] *= customSceneScale;
                node.translation[1] *= customSceneScale;
                node.translation[2] *= customSceneScale;
            }
        }

        std::vector<Vector3> AABBPointsForSceneOBB;

        g_Scene->m_Nodes.resize(m_GLTFData->nodes_count);

        for (uint32_t i = 0; i < m_GLTFData->nodes_count; ++i)
        {
            cgltf_node& node = m_GLTFData->nodes[i];
            Node& newNode = g_Scene->m_Nodes[i];

            Matrix outLocalMatrix;
            cgltf_node_transform_local(&node, (cgltf_float*)&outLocalMatrix);

            verify(outLocalMatrix.Decompose(newNode.m_Scale, newNode.m_Rotation, newNode.m_Position));

            Matrix outWorldMatrix;
            cgltf_node_transform_world(&node, (cgltf_float*)&outWorldMatrix);

            Vector3 worldScale;
            Quaternion worldRotation;
            Vector3 worldPosition;
            verify(outWorldMatrix.Decompose(worldScale, worldRotation, worldPosition));

            if (node.mesh)
            {
                for (const Primitive& primitive : m_SceneMeshPrimitives.at(cgltf_mesh_index(m_GLTFData, node.mesh)))
                {
                    const uint32_t primitiveID = g_Scene->m_Primitives.size();

                    Primitive& newPrimitive = g_Scene->m_Primitives.emplace_back();
                    newPrimitive.m_NodeID = i;
                    newPrimitive.m_MeshIdx = primitive.m_MeshIdx;
                    newPrimitive.m_Material = primitive.m_Material;

                    Mesh& primitiveMesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

                    AABB worldAABB;
                    primitiveMesh.m_AABB.Transform(worldAABB, outWorldMatrix);

                    Vector3 AABBCorners[8];
                    worldAABB.GetCorners(AABBCorners);

                    for (const Vector3& corner : AABBCorners)
                    {
                        AABBPointsForSceneOBB.push_back(corner);
                    }

                    Sphere worldBoundingSphere;
                    primitiveMesh.m_BoundingSphere.Transform(worldBoundingSphere, outWorldMatrix);

                    AABB::CreateMerged(g_Scene->m_AABB, g_Scene->m_AABB, worldAABB);
                    Sphere::CreateMerged(g_Scene->m_BoundingSphere, g_Scene->m_BoundingSphere, worldBoundingSphere);
                }
            }

            if (node.camera)
            {
                assert(node.camera->type == cgltf_camera_type_perspective);

                Scene::Camera& newCamera = g_Scene->m_Cameras.emplace_back();

                newCamera.m_Name = node.name ? node.name : "Un-named Camera";
                newCamera.m_Orientation = worldRotation;
                newCamera.m_Position = worldPosition;
            }

            if (node.light)
            {
                if (node.light->type == cgltf_light_type_directional)
                {
                    g_Scene->m_DirLightVec = -outWorldMatrix.Forward();

                    // Ensure the vector has valid length
                    assert(g_Scene->m_DirLightVec.LengthSquared() <= (1 + kKindaSmallNumber));

                    // Step 1: Calculate m_SunInclination (phi)
                    g_Scene->m_SunInclination = std::asin(g_Scene->m_DirLightVec.y);  // Asin returns radians
                    g_Scene->m_SunInclination = ConvertToDegrees(g_Scene->m_SunInclination); // Convert to degrees

                    // Step 2: Calculate m_SunOrientation (theta)
                    g_Scene->m_SunOrientation = std::atan2(g_Scene->m_DirLightVec.z, g_Scene->m_DirLightVec.x); // Atan2 returns radians
                    g_Scene->m_SunOrientation = ConvertToDegrees(g_Scene->m_SunOrientation); // Convert to degrees
                }
            }

            if (node.parent)
            {
                newNode.m_ParentNodeID = cgltf_node_index(m_GLTFData, node.parent);
            }

            for (uint32_t j = 0; j < node.children_count; ++j)
            {
                newNode.m_ChildrenNodeIDs.push_back(cgltf_node_index(m_GLTFData, node.children[j]));
            }

			//LOG_DEBUG("New Node: [%s]", node.name ? node.name : "Un-named Node");
        }

        if (!AABBPointsForSceneOBB.empty())
        {
            OBB::CreateFromPoints(g_Scene->m_OBB, AABBPointsForSceneOBB.size(), AABBPointsForSceneOBB.data(), sizeof(Vector3));
        }
    }
    
    void LoadAnimations()
    {
        SCENE_LOAD_PROFILE("Load Animations");

        if (m_bHasValidCachedData && !g_Scene->m_Animations.empty())
        {
            // If we have valid cached data, we don't need to load animations again
            return;
        }

        g_Scene->m_Animations.resize(m_GLTFData->animations_count);

        for (uint32_t animationIdx = 0; animationIdx < m_GLTFData->animations_count; ++animationIdx)
        {
            const cgltf_animation& gltfAnimation = m_GLTFData->animations[animationIdx];

            Animation& newAnimation = g_Scene->m_Animations[animationIdx];
            newAnimation.m_Name = gltfAnimation.name ? gltfAnimation.name : "Un-named Animation";

            for (uint32_t channelIdx = 0; channelIdx < gltfAnimation.channels_count; ++channelIdx)
            {
                const cgltf_animation_channel& gltfAnimationChannel = gltfAnimation.channels[channelIdx];
                const cgltf_animation_sampler& gltfSampler = *gltfAnimationChannel.sampler;

                assert(gltfSampler.interpolation == cgltf_interpolation_type_linear); // TODO: support other interpolation types

                if (gltfSampler.input->count < 2)
                {
                    LOG_DEBUG("GLTF - Animation for node '%s' has less than 2 keyframes. Skipping", gltfAnimationChannel.target_node->name);
                    continue;
                }
                assert(gltfSampler.input->count == gltfSampler.output->count);

                Animation::Channel& newChannel = newAnimation.m_Channels.emplace_back();

                assert(gltfAnimationChannel.target_node);
                newChannel.m_TargetNodeIdx = cgltf_node_index(m_GLTFData, gltfAnimationChannel.target_node);

                switch (gltfAnimationChannel.target_path)
                {
                case cgltf_animation_path_type_rotation:    newChannel.m_PathType = Animation::Channel::PathType::Rotation;    break;
                case cgltf_animation_path_type_translation: newChannel.m_PathType = Animation::Channel::PathType::Translation; break;
                case cgltf_animation_path_type_scale:       newChannel.m_PathType = Animation::Channel::PathType::Scale;       break;
                default:
                    // TODO: support other target paths
                    assert(0);
                }

                newChannel.m_KeyFrames.resize(gltfSampler.input->count);
                assert(cgltf_num_components(gltfSampler.input->type) == 1);
                newChannel.m_Data.resize(gltfSampler.output->count);

                // Skip if we have valid cached data. we read the uncompressed data from the cache
                if (m_bHasValidCachedData)
                {
                    continue;
                }

                verify(cgltf_accessor_unpack_floats(gltfSampler.input, newChannel.m_KeyFrames.data(), gltfSampler.input->count));
                const uint32_t nbComponents = cgltf_num_components(gltfSampler.output->type);
                assert(nbComponents <= 4);
                for (uint32_t i = 0; i < gltfSampler.output->count; ++i)
                {
                    const cgltf_bool bResult = cgltf_accessor_read_float(gltfSampler.output, i, (cgltf_float*)&newChannel.m_Data[i], nbComponents);
                    assert(bResult == 1);
                }

                newAnimation.m_TimeStart = std::min(newAnimation.m_TimeStart, newChannel.m_KeyFrames.front());
                newAnimation.m_TimeEnd = std::max(newAnimation.m_TimeEnd, newChannel.m_KeyFrames.back());
            }
        }
    }

    void UploadGlobalMaterialBuffer()
    {
        SCENE_LOAD_PROFILE("Upload Global Material Buffer");

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalMaterialData.size() * sizeof(MaterialData);
            desc.structStride = sizeof(MaterialData);
            desc.debugName = "Global Material Data Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMaterialDataBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "Upload Global Material Buffer");

        LOG_DEBUG("Global material data = [%d] entries, [%f] MB", m_GlobalMaterialData.size(), BYTES_TO_MB(g_Graphic.m_GlobalMaterialDataBuffer->getDesc().byteSize));
        commandList->writeBuffer(g_Graphic.m_GlobalMaterialDataBuffer, m_GlobalMaterialData.data(), g_Graphic.m_GlobalMaterialDataBuffer->getDesc().byteSize);
    }

    void UploadGlobalMeshBuffers(nvrhi::CommandListHandle commandList)
    {
        SCENE_LOAD_PROFILE("Upload Global Mesh Buffers");

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalVertices.size() * sizeof(RawVertexFormat);
            desc.structStride = sizeof(RawVertexFormat);
            desc.debugName = "Global Vertex Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.isAccelStructBuildInput = true;
            g_Graphic.m_GlobalVertexBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalIndices.size() * sizeof(GraphicConstants::IndexBufferFormat_t);
            desc.structStride = sizeof(uint32_t);
            desc.debugName = "Global Index Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.isAccelStructBuildInput = true;
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
            desc.byteSize = m_GlobalMeshletVertexIdxOffsets.size() * sizeof(uint32_t);
            desc.structStride = sizeof(uint32_t);
            desc.debugName = "Global Meshlet Vertex Index Offsets Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMeshletVertexOffsetsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalMeshletIndices.size() * sizeof(uint32_t);
            desc.structStride = sizeof(uint32_t);
            desc.debugName = "Global Meshlet Indices Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMeshletIndicesBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = m_GlobalMeshletDatas.size() * sizeof(MeshletData);
            desc.structStride = sizeof(MeshletData);
            desc.debugName = "Global Meshlet Data Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            g_Graphic.m_GlobalMeshletDataBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        LOG_DEBUG("Global vertices = [%d] vertices, [%f] MB", m_GlobalVertices.size(), BYTES_TO_MB(g_Graphic.m_GlobalVertexBuffer->getDesc().byteSize));
        LOG_DEBUG("Global indices = [%d] indices, [%f] MB", m_GlobalIndices.size(), BYTES_TO_MB(g_Graphic.m_GlobalIndexBuffer->getDesc().byteSize));
        LOG_DEBUG("Global mesh data = [%d] entries, [%f] MB", m_GlobalMeshData.size(), BYTES_TO_MB(g_Graphic.m_GlobalMeshDataBuffer->getDesc().byteSize));
        LOG_DEBUG("Global meshlet vertex idx offsets = [%d] entries, [%f] MB", m_GlobalMeshletVertexIdxOffsets.size(), BYTES_TO_MB(g_Graphic.m_GlobalMeshletVertexOffsetsBuffer->getDesc().byteSize));
        LOG_DEBUG("Global meshlet indices = [%d] entries, [%f] MB", m_GlobalMeshletIndices.size(), BYTES_TO_MB(g_Graphic.m_GlobalMeshletIndicesBuffer->getDesc().byteSize));
        LOG_DEBUG("Global meshlet data = [%d] entries, [%f] MB", m_GlobalMeshletDatas.size(), BYTES_TO_MB(g_Graphic.m_GlobalMeshletDataBuffer->getDesc().byteSize));
        commandList->writeBuffer(g_Graphic.m_GlobalVertexBuffer, m_GlobalVertices.data(), g_Graphic.m_GlobalVertexBuffer->getDesc().byteSize);
        commandList->writeBuffer(g_Graphic.m_GlobalIndexBuffer, m_GlobalIndices.data(), g_Graphic.m_GlobalIndexBuffer->getDesc().byteSize);
        commandList->writeBuffer(g_Graphic.m_GlobalMeshDataBuffer, m_GlobalMeshData.data(), g_Graphic.m_GlobalMeshDataBuffer->getDesc().byteSize);
        commandList->writeBuffer(g_Graphic.m_GlobalMeshletVertexOffsetsBuffer, m_GlobalMeshletVertexIdxOffsets.data(), g_Graphic.m_GlobalMeshletVertexOffsetsBuffer->getDesc().byteSize);
        commandList->writeBuffer(g_Graphic.m_GlobalMeshletIndicesBuffer, m_GlobalMeshletIndices.data(), g_Graphic.m_GlobalMeshletIndicesBuffer->getDesc().byteSize);
        commandList->writeBuffer(g_Graphic.m_GlobalMeshletDataBuffer, m_GlobalMeshletDatas.data(), g_Graphic.m_GlobalMeshletDataBuffer->getDesc().byteSize);
    }

    void WriteCachedData()
    {
        if (m_bHasValidCachedData || m_bIsDefaultScene)
        {
            return;
        }

        PROFILE_FUNCTION();

        ScopedFile cachedDataFile{ m_CachedDataFilePath, "wb" };

        CachedData::Header header;
        header.m_NumVertices = m_GlobalVertices.size();
        header.m_NumIndices = m_GlobalIndices.size();
        header.m_NumMeshes = m_GlobalMeshData.size();
        header.m_NumMeshletVertexIdxOffsets = m_GlobalMeshletVertexIdxOffsets.size();
        header.m_NumMeshletIndices = m_GlobalMeshletIndices.size();
        header.m_NumMeshletDatas = m_GlobalMeshletDatas.size();

        fwrite(&header, sizeof(header), 1, cachedDataFile);
        fwrite(m_GlobalVertices.data(), sizeof(RawVertexFormat), m_GlobalVertices.size(), cachedDataFile);
        fwrite(m_GlobalIndices.data(), sizeof(GraphicConstants::IndexBufferFormat_t), m_GlobalIndices.size(), cachedDataFile);
        fwrite(m_GlobalMeshData.data(), sizeof(MeshData), m_GlobalMeshData.size(), cachedDataFile);
        fwrite(m_GlobalMeshletVertexIdxOffsets.data(), sizeof(uint32_t), m_GlobalMeshletVertexIdxOffsets.size(), cachedDataFile);
        fwrite(m_GlobalMeshletIndices.data(), sizeof(uint32_t), m_GlobalMeshletIndices.size(), cachedDataFile);
        fwrite(m_GlobalMeshletDatas.data(), sizeof(MeshletData), m_GlobalMeshletDatas.size(), cachedDataFile);

        std::vector<CachedData::MeshSpecificData> meshSpecificDataArray;
        meshSpecificDataArray.resize(m_GlobalMeshData.size());
        for (uint32_t i = 0; i < meshSpecificDataArray.size(); ++i)
        {
            CachedData::MeshSpecificData& meshSpecificData = meshSpecificDataArray[i];
            const Mesh& mesh = g_Graphic.m_Meshes.at(i);

            meshSpecificData.m_NumIndices = mesh.m_NumIndices;
            meshSpecificData.m_NumVertices = mesh.m_NumVertices;
            meshSpecificData.m_AABB = mesh.m_AABB;
        }

        fwrite(meshSpecificDataArray.data(), sizeof(CachedData::MeshSpecificData), meshSpecificDataArray.size(), cachedDataFile);

        for (const Animation& animation : g_Scene->m_Animations)
        {
            fwrite(&animation.m_TimeStart, sizeof(float), 1, cachedDataFile);
            fwrite(&animation.m_TimeEnd, sizeof(float), 1, cachedDataFile);

            for (const Animation::Channel& channel : animation.m_Channels)
            {
                fwrite(channel.m_KeyFrames.data(), sizeof(float), channel.m_KeyFrames.size(), cachedDataFile);
                fwrite(channel.m_Data.data(), sizeof(Vector4), channel.m_Data.size(), cachedDataFile);
            }
        }
    }
};

std::unique_ptr<GLTFSceneLoader> gs_GLTFLoader;

void PreloadScene()
{
    gs_GLTFLoader = std::make_unique<GLTFSceneLoader>();
    gs_GLTFLoader->PreloadScene();
}

void LoadScene()
{
    assert(gs_GLTFLoader);

    tf::Taskflow taskflow;

    if (gs_GLTFLoader->m_bHasValidCachedData)
    {
        taskflow.emplace([]
            {
                nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
                SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "UploadGlobalMeshBuffers & BuildBLAS");

                gs_GLTFLoader->UploadGlobalMeshBuffers(commandList);

                for (Mesh& mesh : g_Graphic.m_Meshes)
                {
                    mesh.BuildBLAS(commandList);
                }
            });
    }

    taskflow.emplace([] { gs_GLTFLoader->LoadScene();});

    g_Engine.m_Executor->run(taskflow).wait();
    
    gs_GLTFLoader.reset();
}

#undef SCENE_LOAD_PROFILE
