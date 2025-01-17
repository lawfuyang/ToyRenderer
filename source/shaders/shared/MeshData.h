#ifndef _MESH_DATA_H_
#define _MESH_DATA_H_

#include "ShaderInterop.h"

static const uint32_t kMaxMeshletVertices = 64;
static const uint32_t kMaxMeshletTriangles = 96;
static const uint32_t kMeshletShaderThreadGroupSize = 96;

struct MeshData
{
	Vector4 m_BoundingSphere;
	uint32_t m_MeshletDataBufferIdx;
	uint32_t m_NumMeshlets;
};

struct MeshletData
{
	Vector4 m_BoundingSphere;
	uint32_t m_ConeAxisAndCutoff; // 4x int8_t
	uint32_t m_MeshletVertexIDsBufferIdx;
	uint32_t m_MeshletIndexIDsBufferIdx;
	uint32_t m_VertexAndTriangleCount; // 1x uint8_t + 1x uint8_t
};

struct MeshletPayload
{
	uint32_t m_InstanceConstIdx;
	uint32_t m_MeshletIndices[64];
};

struct MeshletAmplificationData
{
    uint32_t m_InstanceConstIdx;
    uint32_t m_MeshletGroupOffset;
};

#endif
