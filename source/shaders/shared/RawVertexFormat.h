#ifndef _RAW_VERTEX_FORMAT_H_
#define _RAW_VERTEX_FORMAT_H_

#include "StructsCommon.h"

struct RawVertexFormat
{
    Vector3 m_Position;
    uint32_t m_PackedNormal;
    uint32_t m_PackedTangent;
    Half2 m_TexCoord;
};

#endif // #define _RAW_VERTEX_FORMAT_H_
