#ifndef _RAW_VERTEX_FORMAT_H_
#define _RAW_VERTEX_FORMAT_H_

#include "ShaderInterop.h"

struct RawVertexFormat
{
    Vector3 m_Position;
    uint32_t m_PackedNormal;
    Half2 m_TexCoord;
};

#endif // #define _RAW_VERTEX_FORMAT_H_
