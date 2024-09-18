#pragma once

#include "FFXHelpers.h"
#include "RenderGraph.h"

class GPUCulling
{
	FFXHelpers::SPD m_SPDHelper;

public:
	void Setup(RenderGraph& renderGraph);
	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph);
};
