#include "toyrenderer_common.hlsli"

#include "shared/UpdateNodeTransformsStructs.h"
#include "shared/BasePassStructs.h"
#include "shared/MeshData.h"

cbuffer g_UpdateInstanceConstsPassConstantsBuffer : register(b0) { UpdateInstanceConstsPassConstants g_UpdateInstanceConstsPassConstants; }
StructuredBuffer<NodeLocalTransform> g_NodeLocalTransforms : register(t0);
StructuredBuffer<uint> g_PrimitiveIDToNodeIDBuffer : register(t1);
RWStructuredBuffer<BasePassInstanceConstants> g_InstanceConstants : register(u0);
RWStructuredBuffer<TLASInstanceDesc> g_TLASInstanceDescsBuffer : register(u1);

[numthreads(kNumThreadsPerWave, 1, 1)]
void CS_UpdateInstanceConstsAndBuildTLAS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= g_UpdateInstanceConstsPassConstants.m_NumInstances)
    {
        return;
    }
    
    uint instanceID = dispatchThreadID.x;
    uint nodeID = g_PrimitiveIDToNodeIDBuffer[instanceID];
    NodeLocalTransform localTransform = g_NodeLocalTransforms[nodeID];
    
    float4x4 worldMatrix = MakeWorldMatrix(localTransform.m_Position, localTransform.m_Rotation, localTransform.m_Scale);
    
    uint parentIdx = localTransform.m_ParentNodeIdx;
    while (parentIdx != 0xFFFFFFFF)
    {
        NodeLocalTransform parentTransform = g_NodeLocalTransforms[parentIdx];
        
        worldMatrix = mul(worldMatrix, MakeWorldMatrix(parentTransform.m_Position, parentTransform.m_Rotation, parentTransform.m_Scale));
        
        parentIdx = parentTransform.m_ParentNodeIdx;
    }
    
    g_InstanceConstants[instanceID].m_PrevWorldMatrix = g_InstanceConstants[instanceID].m_WorldMatrix;
    g_InstanceConstants[instanceID].m_WorldMatrix = worldMatrix;
    
    TLASInstanceDesc instanceDesc = g_TLASInstanceDescsBuffer[instanceID];
    instanceDesc.m_Transform[0] = worldMatrix._11;
    instanceDesc.m_Transform[1] = worldMatrix._21;
    instanceDesc.m_Transform[2] = worldMatrix._31;
    instanceDesc.m_Transform[3] = worldMatrix._41;
    instanceDesc.m_Transform[4] = worldMatrix._12;
    instanceDesc.m_Transform[5] = worldMatrix._22;
    instanceDesc.m_Transform[6] = worldMatrix._32;
    instanceDesc.m_Transform[7] = worldMatrix._42;
    instanceDesc.m_Transform[8] = worldMatrix._13;
    instanceDesc.m_Transform[9] = worldMatrix._23;
    instanceDesc.m_Transform[10] = worldMatrix._33;
    instanceDesc.m_Transform[11] = worldMatrix._43;
    
    g_TLASInstanceDescsBuffer[instanceID] = instanceDesc;
}
