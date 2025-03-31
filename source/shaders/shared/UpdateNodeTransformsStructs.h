#ifndef _NODE_TRANSFORM_DATA_
#define _NODE_TRANSFORM_DATA_

#include "ShaderInterop.h"

struct NodeLocalTransform
{
    uint32_t m_ParentNodeIdx;
    Vector3 m_Position;
    Quaternion m_Rotation;
    Vector3 m_Scale;
    uint32_t PAD0;
};

struct UpdateInstanceConstsPassConstants
{
    uint32_t m_NumInstances;
};

#endif // #define _NODE_TRANSFORM_DATA_
