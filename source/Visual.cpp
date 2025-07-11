#include "Visual.h"

#include "extern/meshoptimizer/src/meshoptimizer.h"

#include "Engine.h"
#include "Graphic.h"
#include "Scene.h"
#include "Utilities.h"
#include "TextureLoading.h"

#include "shaders/ShaderInterop.h"

void Texture::LoadFromMemory(const void* rawData, const nvrhi::TextureDesc& textureDesc)
{
    PROFILE_FUNCTION();

    assert(!IsValid());
    assert(textureDesc.depth == 1);

    m_NVRHITextureHandle = g_Graphic.m_NVRHIDevice->createTexture(textureDesc);
    m_SRVIndexInTable = g_Graphic.RegisterInSrvUavCbvDescriptorTable(*this);

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    // fill texture data for mip0
    // NOTE: fills each array slice with the same exact src data bytes
    for (uint32_t arraySlice = 0; arraySlice < textureDesc.arraySize; arraySlice++)
    {
        commandList->writeTexture(m_NVRHITextureHandle, arraySlice, 0, rawData, textureDesc.width * nvrhi::getFormatInfo(textureDesc.format).bytesPerBlock);
    }

    commandList->setPermanentTextureState(m_NVRHITextureHandle, textureDesc.isUAV ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
}

void Texture::LoadFromFile(std::string_view filePath)
{
    PROFILE_FUNCTION();

    assert(!IsValid());

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    const std::string debugName = std::filesystem::path{ filePath }.stem().string();
    m_StreamingFilePath = filePath;

    nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
    SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

    ScopedFile scopedFile{filePath.data(), "rb"};
    assert(scopedFile);
    assert(IsDDSImage(scopedFile));

    const DDSFileInfo ddsFileInfo = GetDDSFileInfo(scopedFile);

    m_NumTextureMips = ddsFileInfo.m_MipCount;

    nvrhi::TextureDesc desc;
    desc.width = ddsFileInfo.m_Width;
    desc.height = ddsFileInfo.m_Height;
    desc.format = ddsFileInfo.m_Format;
    desc.isTiled = true;
    desc.mipLevels = m_NumTextureMips;
    desc.debugName = debugName;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    m_NVRHITextureHandle = device->createTexture(desc);

    device->getTextureTiling(m_NVRHITextureHandle, &m_NumTiles, &m_PackedMipDesc, &m_TileShape, &m_NumTextureMips, m_TilingsInfo);

    const uint32_t packedMipIdx = m_PackedMipDesc.numStandardMips;

    nvrhi::HeapDesc packedMipHeapDesc;
    packedMipHeapDesc.capacity = m_PackedMipDesc.numTilesForPackedMips * KB_TO_BYTES(64); // TODO: confirm if Vulkan also uses 64KB tiles
    packedMipHeapDesc.type = nvrhi::HeapType::DeviceLocal;
    packedMipHeapDesc.debugName = "packed mip heap";
    m_MipHeaps[packedMipIdx] = device->createHeap(packedMipHeapDesc);

    nvrhi::BufferDesc packedMipHeapBufferDesc;
    packedMipHeapBufferDesc.byteSize = packedMipHeapDesc.capacity;
    packedMipHeapBufferDesc.isVirtual = true;
    packedMipHeapBufferDesc.initialState = nvrhi::ResourceStates::CopySource;
    packedMipHeapBufferDesc.keepInitialState = true;
    m_MipHeapBuffers[packedMipIdx] = device->createBuffer(packedMipHeapBufferDesc);

    device->bindBufferMemory(m_MipHeapBuffers[packedMipIdx], m_MipHeaps[packedMipIdx], 0);

    DDSReadParams readParams;
    readParams.m_File = scopedFile;
    readParams.m_Texture = this;
    readParams.m_StartMipToRead = packedMipIdx;
    readParams.m_NumMipsToRead = m_PackedMipDesc.numPackedMips;
    ReadPackedDDSMipDatas(ddsFileInfo, readParams);

    std::vector<nvrhi::TiledTextureCoordinate> tiledTextureCoordinates(m_PackedMipDesc.numPackedMips);
    std::vector<nvrhi::TiledTextureRegion> tiledRegions(m_PackedMipDesc.numPackedMips);
    std::vector<uint64_t> byteOffsets(m_PackedMipDesc.numPackedMips);
    for (uint32_t i = 0; i < m_PackedMipDesc.numPackedMips; ++i)
    {
        tiledTextureCoordinates[i].mipLevel = packedMipIdx;
        tiledTextureCoordinates[i].arrayLevel = 0;
        tiledTextureCoordinates[i].x = 0;
        tiledTextureCoordinates[i].y = 0;
        tiledTextureCoordinates[i].z = 0;

        tiledRegions[i].tilesNum = 1;
        tiledRegions[i].width = m_TilingsInfo[packedMipIdx].widthInTiles;
        tiledRegions[i].height = m_TilingsInfo[packedMipIdx].heightInTiles;
        tiledRegions[i].depth = m_TilingsInfo[packedMipIdx].depthInTiles;

        byteOffsets[i] = m_StreamingMipDatas[packedMipIdx + i].m_DataOffset - m_StreamingMipDatas[packedMipIdx].m_DataOffset;
    }

    nvrhi::TextureTilesMapping tileMapping;
    tileMapping.tiledTextureCoordinates = tiledTextureCoordinates.data();
    tileMapping.tiledTextureRegions = tiledRegions.data();
    tileMapping.byteOffsets = byteOffsets.data();
    tileMapping.numTextureRegions = tiledTextureCoordinates.size();
    tileMapping.heap = m_MipHeaps[packedMipIdx];

    device->updateTextureTileMappings(m_NVRHITextureHandle, &tileMapping, 1);

    for (uint32_t i = 0; i < m_PackedMipDesc.numPackedMips; ++i)
    {
        commandList->writeTexture(m_NVRHITextureHandle, 0, packedMipIdx + i, readParams.m_MipDatas[i].m_Data.data(), readParams.m_MipDatas[i].m_MemPitch);
    }

    m_SRVIndexInTable = g_Graphic.RegisterInSrvUavCbvDescriptorTable(*this);

    const nvrhi::TextureDesc& texDesc = m_NVRHITextureHandle->getDesc();
    LOG_DEBUG("New Texture: %s, %d x %d, %s", texDesc.debugName.c_str(), texDesc.width, texDesc.height, nvrhi::utils::FormatToString(texDesc.format));
}

bool Texture::IsValid() const
{
    return m_NVRHITextureHandle != nullptr
        && m_SRVIndexInTable != UINT_MAX
        && m_CurrentlyStreamedMip != UINT_MAX;
}

bool Primitive::IsValid() const
{
    return m_NodeID != UINT_MAX
        && g_Graphic.m_Meshes.at(m_MeshIdx).IsValid()
        && m_Material.IsValid();
}

uint32_t Mesh::PackNormal(const Vector3& normal)
{
    Vector3 v = normal;

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
    return (xInt << 20) | (yInt << 10) | (zInt);
}

void Mesh::Initialize(
    const std::vector<RawVertexFormat>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t globalVertexBufferIdx,
    uint32_t globalIndexBufferIdxOffset,
    std::vector<uint32_t>& meshletVertexIdxOffsetsOut,
    std::vector<uint32_t>& meshletIndicesOut,
    std::vector<MeshletData>& meshletsOut,
    std::string_view meshName)
{
    PROFILE_FUNCTION();

    m_GlobalIndexBufferIdx = globalIndexBufferIdxOffset;
    m_GlobalVertexBufferIdx = globalVertexBufferIdx;
    m_NumIndices = indices.size();
    m_NumVertices = vertices.size();
    m_DebugName = meshName;

    // init BV(s)
    Sphere::CreateFromPoints(m_BoundingSphere, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));
    AABB::CreateFromPoints(m_AABB, vertices.size(), (const DirectX::XMFLOAT3*)vertices.data(), sizeof(RawVertexFormat));

    const float LODErrorScalingFactor = meshopt_simplifyScale(&vertices[0].m_Position.x, vertices.size(), sizeof(RawVertexFormat));

    std::vector<uint32_t> LODIndices = indices;
    float LODError = 0.0f;
    bool bSimplifySloppy = false;

    for (uint32_t lodIdx = 0; lodIdx < GraphicConstants::kMaxNumMeshLODs; ++lodIdx)
    {
        PROFILE_SCOPED("Process LOD");

        // note: we're using the same 'kTargetError' value for all LODs; if this changes, we need to remove/change 'kMinIndexReductionPercentage' exit criteria
        static const float kTargetError = 0.1f;
        static const float kTargetIndexCountPercentage = 0.65f;
        static const float kMinIndexReductionPercentage = 0.95f;
        static const uint32_t kSimplifyOptions = 0;
        static const Vector3 kAttributeWeights{ 1.0f, 1.0f, 1.0f };
        static const unsigned char* kVertexLock = nullptr;
        static const float kMeshletConeWeight = 0.25f;

        MeshLOD& newLOD = m_LODs[m_NumLODs++];
        newLOD.m_NumIndices = LODIndices.size();
        newLOD.m_MeshletDataBufferIdx = meshletsOut.size(); // NOTE: this will be properly offset at the global level after all mesh data are loaded
        newLOD.m_Error = LODError * LODErrorScalingFactor;

        std::vector<meshopt_Meshlet> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint8_t> meshletTriangles;

        const uint32_t numMaxMeshlets = meshopt_buildMeshletsBound(LODIndices.size(), kMaxMeshletVertices, kMaxMeshletTriangles);
        meshlets.resize(numMaxMeshlets);
        meshletVertices.resize(numMaxMeshlets * kMaxMeshletVertices);
        meshletTriangles.resize(numMaxMeshlets * kMaxMeshletTriangles * 3);

        {
            PROFILE_SCOPED("Build Meshlets");

            newLOD.m_NumMeshlets = meshopt_buildMeshlets(
                meshlets.data(),
                meshletVertices.data(),
                meshletTriangles.data(),
                LODIndices.data(),
                LODIndices.size(),
                (const float*)vertices.data(),
                vertices.size(),
                sizeof(RawVertexFormat),
                kMaxMeshletVertices,
                kMaxMeshletTriangles,
                kMeshletConeWeight);
        }

        meshlets.resize(newLOD.m_NumMeshlets);

        {
            PROFILE_SCOPED("Generate MeshletDatas");

            for (const meshopt_Meshlet& meshlet : meshlets)
            {
                meshopt_optimizeMeshlet(&meshletVertices.at(meshlet.vertex_offset), &meshletTriangles.at(meshlet.triangle_offset), meshlet.triangle_count, meshlet.vertex_count);

                MeshletData& newMeshlet = meshletsOut.emplace_back();

                // NOTE: these will be properly offset to the global value after all mesh data are loaded
                newMeshlet.m_MeshletVertexIDsBufferIdx = meshletVertexIdxOffsetsOut.size();
                newMeshlet.m_MeshletIndexIDsBufferIdx = meshletIndicesOut.size();

                for (uint32_t i = 0; i < meshlet.vertex_count; ++i)
                {
                    meshletVertexIdxOffsetsOut.push_back(globalVertexBufferIdx + meshletVertices.at(meshlet.vertex_offset + i));
                }

                for (uint32_t i = 0; i < meshlet.triangle_count; ++i)
                {
                    const uint32_t baseOffset = meshlet.triangle_offset + (i * 3);
                    const uint8_t a = meshletTriangles.at(baseOffset + 0);
                    const uint8_t b = meshletTriangles.at(baseOffset + 1);
                    const uint8_t c = meshletTriangles.at(baseOffset + 2);

                    const uint32_t packedIndices = a | (b << 8) | (c << 16);

                    meshletIndicesOut.push_back(packedIndices);
                }

                const meshopt_Bounds meshletBounds = meshopt_computeMeshletBounds(
                    &meshletVertices.at(meshlet.vertex_offset),
                    &meshletTriangles.at(meshlet.triangle_offset),
                    meshlet.triangle_count,
                    (const float*)vertices.data(),
                    vertices.size(),
                    sizeof(RawVertexFormat));

                assert(meshlet.vertex_count <= UINT8_MAX);
                assert(meshlet.triangle_count <= UINT8_MAX);
                assert(Vector3{ meshletBounds.cone_axis }.Length() < (1.0f + kKindaSmallNumber));
                assert(meshletBounds.cone_cutoff_s8 <= (UINT8_MAX / 2));

                newMeshlet.m_VertexAndTriangleCount = meshlet.vertex_count | (meshlet.triangle_count << 8);
                newMeshlet.m_BoundingSphere = Vector4{ meshletBounds.center[0], meshletBounds.center[1], meshletBounds.center[2], meshletBounds.radius };

                const uint32_t packedAxisX = (meshletBounds.cone_axis[0] + 1.0f) * 0.5f * UINT8_MAX;
                const uint32_t packedAxisY = (meshletBounds.cone_axis[1] + 1.0f) * 0.5f * UINT8_MAX;
                const uint32_t packedAxisZ = (meshletBounds.cone_axis[2] + 1.0f) * 0.5f * UINT8_MAX;
                const uint32_t packedCutoff = meshletBounds.cone_cutoff_s8 * 2;

                assert(packedAxisX <= UINT8_MAX);
                assert(packedAxisY <= UINT8_MAX);
                assert(packedAxisZ <= UINT8_MAX);
                assert(packedCutoff <= UINT8_MAX);

                newMeshlet.m_ConeAxisAndCutoff = packedAxisX | (packedAxisY << 8) | (packedAxisZ << 16) | (packedCutoff << 24);
            }
        }

        {
            PROFILE_SCOPED("Simplify Mesh");

            std::vector<Vector3> unpackedNormals;
            for (const RawVertexFormat& v : vertices)
            {
                // Unccale to 10-bit integers from [0-1023] > [0-1]
                const uint32_t xInt = (uint32_t)(v.m_PackedNormal >> 20) & 0x3FF;
                const uint32_t yInt = (uint32_t)(v.m_PackedNormal >> 10) & 0x3FF;
                const uint32_t zInt = (uint32_t)(v.m_PackedNormal >> 0) & 0x3FF;

                // Unnormalize x, y, z from [0, 1] to [-1, 1]
                Vector3& unpackedNormal = unpackedNormals.emplace_back();

                unpackedNormal = Vector3{ (float)xInt / 1023.0f, (float)yInt / 1023.0f, (float)zInt / 1023.0f };
                unpackedNormal = (unpackedNormal * 2.0f) - Vector3::One;
            }

            float resultError = 0.0f;

            size_t numSimplifiedIndices = 0;
            const size_t targetIndexCount = (size_t(double(LODIndices.size()) * kTargetIndexCountPercentage) / 3) * 3;
            if (bSimplifySloppy)
            {
                numSimplifiedIndices = meshopt_simplifySloppy(
                    LODIndices.data(),
                    LODIndices.data(),
                    LODIndices.size(),
                    (const float*)vertices.data(),
                    vertices.size(),
                    sizeof(RawVertexFormat),
                    targetIndexCount,
                    kTargetError,
                    &resultError);
            }
            else
            {
                numSimplifiedIndices = meshopt_simplifyWithAttributes(
                    LODIndices.data(),
                    LODIndices.data(),
                    LODIndices.size(),
                    (const float*)vertices.data(),
                    vertices.size(),
                    sizeof(RawVertexFormat),
                    (const float*)unpackedNormals.data(),
                    sizeof(Vector3),
                    &kAttributeWeights.x,
                    sizeof(Vector3) / sizeof(float),
                    kVertexLock,
                    targetIndexCount,
                    kTargetError,
                    kSimplifyOptions,
                    &resultError);
            }

            assert(numSimplifiedIndices <= LODIndices.size());

            // we've reached the error bound, next LOD onwards will use sloppy simplification
            if (numSimplifiedIndices == LODIndices.size() || numSimplifiedIndices == 0 || (numSimplifiedIndices >= size_t(double(LODIndices.size()) * kMinIndexReductionPercentage)))
            {
                bSimplifySloppy = true;
            }
            
            LODIndices.resize(numSimplifiedIndices);
            LODError = std::max(LODError, resultError); // important! since we start from last LOD, we need to accumulate the error
        }

        meshopt_optimizeVertexCache(LODIndices.data(), LODIndices.data(), LODIndices.size(), vertices.size());
    }

    std::string logStr = StringFormat("New Mesh: %s, Vertices: %d", meshName.data(), vertices.size());

    static const bool kbDebugLODDetails = false;
    if constexpr (kbDebugLODDetails)
    {
        for (uint32_t i = 0; i < m_NumLODs; ++i)
        {
            logStr += StringFormat("\n\tLOD %d, Indices: %d, MeshletDataBufferIdx: %d, Meshlets: %d, Error: %.2f",
                i, m_LODs[i].m_NumIndices, m_LODs[i].m_MeshletDataBufferIdx, m_LODs[i].m_NumMeshlets, m_LODs[i].m_Error);
        }
    }

    LOG_DEBUG("%s", logStr.c_str());
}

void Mesh::BuildBLAS(nvrhi::CommandListHandle commandList)
{
    // if we already have BLAS, this means that we already have cached mesh data, and we already loaded the BLAS earlier
    if (m_BLAS)
    {
        return;
    }

    PROFILE_FUNCTION();

    nvrhi::rt::GeometryDesc geometryDesc;
    nvrhi::rt::GeometryTriangles& geometryTriangle = geometryDesc.geometryData.triangles;
    geometryTriangle.indexBuffer = g_Graphic.m_GlobalIndexBuffer;
    geometryTriangle.vertexBuffer = g_Graphic.m_GlobalVertexBuffer;
    geometryTriangle.indexFormat = GraphicConstants::kIndexBufferFormat;
    geometryTriangle.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    geometryTriangle.indexOffset = m_GlobalIndexBufferIdx * nvrhi::getFormatInfo(geometryTriangle.indexFormat).bytesPerBlock;
    geometryTriangle.vertexOffset = m_GlobalVertexBufferIdx * sizeof(RawVertexFormat);
    geometryTriangle.indexCount = m_NumIndices;
    geometryTriangle.vertexCount = m_NumVertices;
    geometryTriangle.vertexStride = sizeof(RawVertexFormat);

    geometryDesc.flags = nvrhi::rt::GeometryFlags::None; // can't be opaque since we have alpha tested materials that be applied to this mesh
    geometryDesc.geometryType = nvrhi::rt::GeometryType::Triangles;

    nvrhi::rt::AccelStructDesc blasDesc;
    blasDesc.bottomLevelGeometries = { geometryDesc };
    blasDesc.debugName = StringFormat("%s BLAS", m_DebugName.c_str());
    blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::AllowCompaction;

    m_BLAS = g_Graphic.m_NVRHIDevice->createAccelStruct(blasDesc);

    nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);
}

bool Mesh::IsValid() const
{
    bool bResult = true;

    bResult &= m_NumLODs > 0;

    for (uint32_t i = 0; i < m_NumLODs; ++i)
    {
        bResult &= m_LODs[i].m_MeshletDataBufferIdx != UINT_MAX;
    }

    bResult &= m_MeshDataBufferIdx != UINT_MAX;

    bResult &= !!m_BLAS;

    return bResult;
}

bool Material::IsValid() const
{
    bool bResult = true;

    if (m_MaterialFlags & MaterialFlag_UseDiffuseTexture)
    {
        bResult &= m_Albedo.IsValid();
    }
    if (m_MaterialFlags & MaterialFlag_UseNormalTexture)
    {
        bResult &= m_Normal.IsValid();
    }
    if (m_MaterialFlags & MaterialFlag_UseMetallicRoughnessTexture)
    {
        bResult &= m_MetallicRoughness.IsValid();
    }
    if (m_MaterialFlags & MaterialFlag_UseEmissiveTexture)
    {
        bResult &= m_Emissive.IsValid();
    }

    bResult &= m_MaterialDataBufferIdx != UINT_MAX;

    return bResult;
}

Matrix Node::MakeLocalToWorldMatrix() const
{
    const Matrix translateMat = Matrix::CreateTranslation(m_Position);
    const Matrix rotationMat = Matrix::CreateFromQuaternion(m_Rotation);
    const Matrix scaleMat = Matrix::CreateScale(m_Scale);

    Matrix worldMatrix = rotationMat * scaleMat * translateMat;

    if (m_ParentNodeID != UINT_MAX)
    {
        worldMatrix *= g_Scene->m_Nodes.at(m_ParentNodeID).MakeLocalToWorldMatrix();
    }

    return worldMatrix;
}
