#ifndef _MESH_DATA_H_
#define _MESH_DATA_H_

#include "ShaderInterop.h"

#include "CommonConsts.h"

struct MeshLODData
{
	uint32_t m_MeshletDataBufferIdx;
	uint32_t m_NumMeshlets;
	float m_Error;
	uint32_t PAD0;
};

struct MeshData
{
	Vector4 m_BoundingSphere;
    MeshLODData m_MeshLODDatas[kMaxNumMeshLODs];
	uint32_t m_NumLODs;
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
	uint32_t m_MeshletIndices[64];
	uint32_t m_InstanceConstIdx;
	uint32_t m_MeshLOD;
};

struct MeshletAmplificationData
{
    uint32_t m_InstanceConstIdx;
	uint32_t m_MeshLOD;
    uint32_t m_MeshletGroupOffset;
};

#endif
