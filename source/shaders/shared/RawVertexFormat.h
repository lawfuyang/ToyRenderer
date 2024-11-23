#ifndef _RAW_VERTEX_FORMAT_H_
#define _RAW_VERTEX_FORMAT_H_

#include "StructsCommon.h"

struct RawVertexFormat
{
    Vector3 m_Position;
    uint32_t m_PackedNormal;
    uint32_t m_PackedTangent;
    uint32_t m_PackedTexCoord;
};

#endif // #define _RAW_VERTEX_FORMAT_H_
