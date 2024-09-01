#pragma once

#include "nvrhi/nvrhi.h"

#include "RenderGraph.h"

namespace FFXHelpers
{
	class SPD
	{
	public:
		void CreateTransientResources(RenderGraph& renderGraph);

		void Execute(nvrhi::CommandListHandle commandList,
			const RenderGraph& renderGraph,
			nvrhi::TextureHandle src,
			nvrhi::TextureHandle dest,
			nvrhi::SamplerReductionType reductionType);

		RenderGraph::ResourceHandle m_AtomicRDGBufferHandle;
	};
};
