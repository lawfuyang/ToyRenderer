#include "GPUCulling.h"

void GPUCulling::Setup(RenderGraph& renderGraph)
{
	m_SPDHelper.CreateTransientResources(renderGraph);
}

void GPUCulling::Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
{

}
