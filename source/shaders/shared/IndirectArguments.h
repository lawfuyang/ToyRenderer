#ifndef _INDIRECT_ARGUMENTS_H_
#define _INDIRECT_ARGUMENTS_H_

#include "ShaderInterop.h"

struct DrawIndirectArguments
{
	uint32_t m_VertexCount;
	uint32_t m_InstanceCount;
	uint32_t m_StartVertexLocation;
	uint32_t m_StartInstanceLocation;
};

struct DrawIndexedIndirectArguments
{
	uint32_t m_IndexCount;
	uint32_t m_InstanceCount;
	uint32_t m_StartIndexLocation;
	int32_t  m_BaseVertexLocation;
	uint32_t m_StartInstanceLocation;
};

struct DispatchIndirectArguments
{
	uint32_t m_ThreadGroupCountX;
	uint32_t m_ThreadGroupCountY;
	uint32_t m_ThreadGroupCountZ;
};

#endif // _INDIRECT_ARGUMENTS_H_
