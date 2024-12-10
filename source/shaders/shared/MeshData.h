#ifndef _MESH_DATA_H_
#define _MESH_DATA_H_

#include "StructsCommon.h"

// Maximum number of vertices and triangles in a meshlet
static const uint32_t kMeshletMaxVertices = 64;
static const uint32_t kMeshletMaxTriangles = 96;

struct MeshData
{
	uint32_t m_IndexCount;
	uint32_t m_StartIndexLocation;
	uint32_t m_StartVertexLocation;
	uint32_t PAD0;
	Vector3 m_AABBCenter;
	uint32_t PAD1;
	Vector3 m_AABBExtents;
	uint32_t PAD2;
	Vector4 m_BoundingSphere;
};

#endif
