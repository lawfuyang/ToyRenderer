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

struct TLASInstanceDesc
{
    float m_Transform[12]; // 3x4 matrix flattened
    uint32_t m_InstanceID : 24;
    uint32_t m_InstanceMask : 8;
    uint32_t m_InstanceContributionToHitGroupIndex : 24;
    uint32_t m_Flags : 8;
    uint64_t m_AccelerationStructure;
};

struct UpdateInstanceConstsPassConstants
{
    uint32_t m_NumInstances;
};

#endif // #define _NODE_TRANSFORM_DATA_
