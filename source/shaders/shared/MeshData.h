#ifndef _MESH_DATA_H_
#define _MESH_DATA_H_

#include "StructsCommon.h"

static const uint32_t kMaxMeshletSize = 64;

struct MeshData
{
	Vector4 m_BoundingSphere;
	uint32_t m_IndexCount;
	uint32_t m_StartIndexLocation;
	uint32_t m_StartVertexLocation;
	uint32_t m_MeshletDataOffset;
	uint32_t m_MeshletCount;
};

struct MeshletData
{
	Vector4 m_BoundingSphere;
	uint32_t m_ConeAxisAndCutoff; // 4x int8_t
	uint32_t m_VertexBufferIdx;
	uint32_t m_IndicesBufferIdx;
	uint32_t m_VertexAndTriangleCount; // 1x uint8_t + 1x uint8_t
};

struct MeshletPayload
{
	uint32_t m_InstanceConstIdx;
	uint32_t m_MeshletIndices[64];
};

#endif
