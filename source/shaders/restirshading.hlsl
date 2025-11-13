#include "toyrenderer_common.hlsli"

#include "RtxdiApplicationBridge.hlsli"
#include "Rtxdi/DI/InitialSampling.hlsli"

[numthreads(8, 8, 1)]
void CS_Main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{

}
