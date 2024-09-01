#pragma once

#include "MathUtilities.h"
#include "RenderGraph.h"

#include "nvrhi/nvrhi.h"

class TileRenderingHelper
{
public:
    // TODO: support other tile sizes?
    static const uint32_t kTileSize = 8;

    void Initialize(Vector2U screenDimensions, uint32_t nbTilesTypes);
    void CreateTransientResources(RenderGraph& renderGraph);
    void AddReadDependencies(RenderGraph& renderGraph);
    void ClearBuffers(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) const;

    // (b99) = TileRenderingConsts
    // (t99) = TileOffsetsBuffer
    void DrawTiles(
        nvrhi::CommandListHandle commandList,
        const RenderGraph& renderGraph,
        std::string_view pixelShaderName,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        const nvrhi::FramebufferDesc& frameBufferDesc,
        uint32_t tileID,
        const nvrhi::BlendState::RenderTarget* blendState = nullptr,
        const nvrhi::DepthStencilState* depthStencilState = nullptr,
        const void* pushConstantsData = nullptr,
        size_t pushConstantsBytes = 0) const;

    // (t99) = TileOffsetsBuffer
    void DispatchTiles(
        nvrhi::CommandListHandle commandList,
        const RenderGraph& renderGraph,
        std::string_view shaderName,
        const nvrhi::BindingSetDesc& bindingSetDesc,
        uint32_t tileID,
        const void* pushConstantsData = nullptr,
        size_t pushConstantsBytes = 0) const;

    RenderGraph::ResourceHandle m_DispatchIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_DrawIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_TileCounterRDGBufferHandle;
    RenderGraph::ResourceHandle m_TileOffsetsRDGBufferHandle; // in texels

    Vector2U m_ScreenDimensions;
    Vector3U m_GroupCount;
    uint32_t m_NbTiles = 0;
    uint32_t m_NbTilesTypes = 0;
};
