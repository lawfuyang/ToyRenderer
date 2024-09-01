#include "TileRenderingHelper.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Graphic.h"

#include "shaders/shared/IndirectArguments.h"
#include "shaders/shared/TileRenderingStructs.h"

void TileRenderingHelper::Initialize(Vector2U screenDimensions, uint32_t nbTilesTypes)
{
    PROFILE_SCOPED();

    m_ScreenDimensions = screenDimensions;
    m_GroupCount = ComputeShaderUtils::GetGroupCount(m_ScreenDimensions, Vector2U{ kTileSize, kTileSize });
    m_NbTiles = m_GroupCount.x * m_GroupCount.y * m_GroupCount.z;
    m_NbTilesTypes = nbTilesTypes;
}

void TileRenderingHelper::CreateTransientResources(RenderGraph& renderGraph)
{
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(DispatchIndirectArguments) * m_NbTilesTypes;
        desc.structStride = sizeof(DispatchIndirectArguments);
        desc.debugName = "DispatchIndirectArguments";
        desc.canHaveUAVs = true;
        desc.isDrawIndirectArgs = true;
        desc.initialState = nvrhi::ResourceStates::IndirectArgument;

        renderGraph.CreateTransientResource(m_DispatchIndirectArgsRDGBufferHandle, desc);
    }

    
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(DrawIndirectArguments) * m_NbTilesTypes;
        desc.structStride = sizeof(DrawIndirectArguments);
        desc.debugName = "DrawIndirectArguments";
        desc.canHaveUAVs = true;
        desc.isDrawIndirectArgs = true;
        desc.initialState = nvrhi::ResourceStates::IndirectArgument;

        renderGraph.CreateTransientResource(m_DrawIndirectArgsRDGBufferHandle, desc);
    }

    
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(uint32_t) * m_NbTilesTypes;
        desc.structStride = sizeof(uint32_t);
        desc.debugName = "TileCounterBuffer";
        desc.canHaveUAVs = true;
        desc.isDrawIndirectArgs = true;
        desc.initialState = nvrhi::ResourceStates::IndirectArgument;

        renderGraph.CreateTransientResource(m_TileCounterRDGBufferHandle, desc);
    }

    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(Vector2U) * m_NbTiles * m_NbTilesTypes;
        desc.structStride = sizeof(Vector2U);
        desc.debugName = "TileOffsetsBuffer";
        desc.canHaveUAVs = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        renderGraph.CreateTransientResource(m_TileOffsetsRDGBufferHandle, desc);
    }
}

void TileRenderingHelper::AddReadDependencies(RenderGraph& renderGraph)
{
    renderGraph.AddReadDependency(m_DispatchIndirectArgsRDGBufferHandle);
	renderGraph.AddReadDependency(m_DrawIndirectArgsRDGBufferHandle);
	renderGraph.AddReadDependency(m_TileCounterRDGBufferHandle);
	renderGraph.AddReadDependency(m_TileOffsetsRDGBufferHandle);

}

void TileRenderingHelper::ClearBuffers(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) const
{
    nvrhi::BufferHandle tileCounterBuffer = renderGraph.GetBuffer(m_TileCounterRDGBufferHandle);
    nvrhi::BufferHandle dispatchIndirectArgsBuffer = renderGraph.GetBuffer(m_DispatchIndirectArgsRDGBufferHandle);
    nvrhi::BufferHandle drawIndirectArgsBuffer = renderGraph.GetBuffer(m_DrawIndirectArgsRDGBufferHandle);

    commandList->clearBufferUInt(tileCounterBuffer, 0);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, dispatchIndirectArgsBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(1, drawIndirectArgsBuffer),
    };

    g_Graphic.AddComputePass(commandList, "tilerenderingutils_CS_ClearIndirectParams", bindingSetDesc, ComputeShaderUtils::GetGroupCount(m_NbTilesTypes, 1));
}

void TileRenderingHelper::DrawTiles(
    nvrhi::CommandListHandle commandList,
    const RenderGraph& renderGraph,
    std::string_view pixelShaderName,
    const nvrhi::BindingSetDesc& bindingSetDesc,
    const nvrhi::FramebufferDesc& frameBufferDesc,
    uint32_t tileID,
    const nvrhi::BlendState::RenderTarget* blendStateIn,
    const nvrhi::DepthStencilState* depthStencilState,
    const void* pushConstantsData,
    size_t pushConstantsBytes) const
{
    PROFILE_FUNCTION();
    PROFILE_GPU_SCOPED(commandList, StringFormat("[%s] - tileID:[%d]", pixelShaderName.data(), tileID));

    nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

    TileRenderingConsts tileRenderingConsts;
    tileRenderingConsts.m_OutputDimensions = m_ScreenDimensions;
    tileRenderingConsts.m_TileSize = kTileSize;
    tileRenderingConsts.m_NbTiles = m_NbTiles;
    tileRenderingConsts.m_TileID = tileID;

    nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateVolatileConstantBuffer(commandList, tileRenderingConsts);
    nvrhi::BufferHandle tileCounterBuffer = renderGraph.GetBuffer(m_TileCounterRDGBufferHandle);
    nvrhi::BufferHandle drawIndirectArgsBuffer = renderGraph.GetBuffer(m_DrawIndirectArgsRDGBufferHandle);
    nvrhi::BufferHandle tileOffsetsBuffer = renderGraph.GetBuffer(m_TileOffsetsRDGBufferHandle);

    // add TileRenderingConsts to (b99) & TileOffsetsBuffer to (t99)
    nvrhi::BindingSetDesc bindingSetDescCopy = bindingSetDesc;
    bindingSetDescCopy.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(99, passConstantBuffer));
    bindingSetDescCopy.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(99, tileOffsetsBuffer));

    nvrhi::BindingSetHandle bindingSet;
    nvrhi::BindingLayoutHandle bindingLayout;
    g_Graphic.CreateBindingSetAndLayout(bindingSetDescCopy, bindingSet, bindingLayout);

    const nvrhi::BlendState::RenderTarget& blendState = blendStateIn ? *blendStateIn : g_CommonResources.BlendOpaque;
    const nvrhi::DepthStencilState& depthStencil = depthStencilState ? *depthStencilState : g_CommonResources.DepthNoneStencilNone;

    // PSO
    nvrhi::GraphicsPipelineDesc PSODesc;
    PSODesc.VS = g_Graphic.GetShader("tilerenderingutils_VS_Main");
    PSODesc.PS = g_Graphic.GetShader(pixelShaderName);
    PSODesc.renderState = nvrhi::RenderState{ nvrhi::BlendState{ blendState }, depthStencil, g_CommonResources.CullNone };
    PSODesc.bindingLayouts = { bindingLayout };

    nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

    const nvrhi::TextureDesc& renderTargetDesc = frameBufferDesc.colorAttachments.at(0).texture->getDesc();

    nvrhi::GraphicsState drawState;
    drawState.framebuffer = frameBuffer;
    drawState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)renderTargetDesc.width, (float)renderTargetDesc.height });
    drawState.bindings = { bindingSet };
    drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
    drawState.indirectParams = drawIndirectArgsBuffer;
    drawState.indirectCountBuffer = tileCounterBuffer;

    commandList->setGraphicsState(drawState);

    if (pushConstantsData)
    {
        commandList->setPushConstants(pushConstantsData, pushConstantsBytes);
    }

    const uint32_t indirectArgsBufferOffsetBytes = sizeof(DrawIndirectArguments) * tileID;
    const uint32_t countBufferOffsetBytes = sizeof(uint32_t) * tileID;

    commandList->drawIndirect(indirectArgsBufferOffsetBytes, 1, countBufferOffsetBytes);
}

void TileRenderingHelper::DispatchTiles(
    nvrhi::CommandListHandle commandList,
    const RenderGraph& renderGraph,
    std::string_view shaderName,
    const nvrhi::BindingSetDesc& bindingSetDesc,
    uint32_t tileID,
    const void* pushConstantsData,
    size_t pushConstantsBytes) const
{
    const char* shaderNameWithTileID = StringFormat("[%s] - tileID:[%d]", shaderName.data(), tileID);
    PROFILE_GPU_SCOPED(commandList, shaderNameWithTileID);

    nvrhi::BufferHandle tileCounterBuffer = renderGraph.GetBuffer(m_TileCounterRDGBufferHandle);
    nvrhi::BufferHandle dispatchIndirectArgsBuffer = renderGraph.GetBuffer(m_DispatchIndirectArgsRDGBufferHandle);
    nvrhi::BufferHandle tileOffsetsBuffer = renderGraph.GetBuffer(m_TileOffsetsRDGBufferHandle);

    // add TileOffsetsBuffer to (t99)
    nvrhi::BindingSetDesc bindingSetDescCopy = bindingSetDesc;
    bindingSetDescCopy.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(99, tileOffsetsBuffer));

    const uint32_t indirectArgsBufferOffsetBytes = sizeof(DispatchIndirectArguments) * tileID;
    const uint32_t countBufferOffsetBytes = sizeof(uint32_t) * tileID;

    g_Graphic.AddComputePass(
        commandList,
        shaderName,
        bindingSetDescCopy,
        dispatchIndirectArgsBuffer,
        indirectArgsBufferOffsetBytes,
        tileCounterBuffer,
        countBufferOffsetBytes,
        pushConstantsData,
        pushConstantsBytes);
}
