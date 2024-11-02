#ifndef _MESH_DATA_H_
#define _MESH_DATA_H_

#include "StructsCommon.h"

struct MeshData
{
	uint32_t m_IndexCount;
	uint32_t m_StartIndexLocation;
	uint32_t m_StartVertexLocation;
	uint32_t m_HasTangentData;
	Vector3 m_AABBCenter;
	Vector3 m_AABBExtents;
	Vector4 m_BoundingSphere;
};

#endif
