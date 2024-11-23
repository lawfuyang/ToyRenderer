#ifndef _RAW_VERTEX_FORMAT_H_
#define _RAW_VERTEX_FORMAT_H_

#include "StructsCommon.h"

struct RawVertexFormat
{
    Vector3 m_Position;
    UByte4N m_Normal;
    UByte4N m_Tangent;
    Half2 m_TexCoord;
};

#endif // #define _RAW_VERTEX_FORMAT_H_
