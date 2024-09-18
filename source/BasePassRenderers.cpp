#include "Graphic.h"

#include "CommonResources.h"
#include "DescriptorTableManager.h"
#include "Engine.h"
#include "FFXHelpers.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/shared/BasePassStructs.h"
#include "shaders/shared/CommonConsts.h"
#include "shaders/shared/GPUCullingStructs.h"
#include "shaders/shared/IndirectArguments.h"
#include "shaders/shared/MinMaxDownsampleStructs.h"

static_assert(sizeof(DrawIndirectArguments) == sizeof(nvrhi::DrawIndirectArguments));
static_assert(sizeof(DrawIndirectArguments) == sizeof(D3D12_DRAW_ARGUMENTS));
static_assert(sizeof(DrawIndexedIndirectArguments) == sizeof(nvrhi::DrawIndexedIndirectArguments));
static_assert(sizeof(DrawIndexedIndirectArguments) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

static_assert(SamplerIdx_AnisotropicClamp == (int)nvrhi::SamplerAddressMode::Clamp);
static_assert(SamplerIdx_AnisotropicWrap == (int)nvrhi::SamplerAddressMode::Wrap);
static_assert(SamplerIdx_AnisotropicBorder == (int)nvrhi::SamplerAddressMode::Border);
static_assert(SamplerIdx_AnisotropicMirror == (int)nvrhi::SamplerAddressMode::Mirror);

RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
RenderGraph::ResourceHandle g_GBufferBRDGTextureHandle;
RenderGraph::ResourceHandle g_GBufferCRDGTextureHandle;
RenderGraph::ResourceHandle g_ShadowMapArrayRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class BasePassRenderer : public IRenderer
{
public:
    BasePassRenderer(const char* rendererName) : IRenderer(rendererName) {}

    struct RenderBasePassParams
    {
        nvrhi::ShaderHandle m_PS;
        View* m_View;
        nvrhi::RenderState m_RenderState;
        nvrhi::FramebufferDesc m_FrameBufferDesc;

        nvrhi::BufferHandle m_OverrideInstanceCountBuffer;
        nvrhi::BufferHandle m_OverrideStartInstanceConstsOffsetsBuffer;
        nvrhi::BufferHandle m_OverrideDrawIndexedIndirectArgumentsBuffer;
    };

    void RenderBasePass(nvrhi::CommandListHandle commandList, const RenderBasePassParams& params)
    {
        assert(params.m_View);

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& view = *params.m_View;

        assert(!scene->m_VisualProxies.empty());

        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(params.m_FrameBufferDesc);
        const nvrhi::FramebufferAttachment& depthAttachment = params.m_FrameBufferDesc.depthAttachment;
        const nvrhi::TextureDesc& viewportTexDesc = depthAttachment.texture ? depthAttachment.texture->getDesc() : params.m_FrameBufferDesc.colorAttachments[0].texture->getDesc();

        // pass consts
        BasePassConstants basePassConstants;
        basePassConstants.m_ViewProjMatrix = view.m_ViewProjectionMatrix;
        basePassConstants.m_DirectionalLightVector = scene->m_DirLightVec * scene->m_DirLightStrength;
        basePassConstants.m_DirectionalLightColor = scene->m_DirLightColor;
        basePassConstants.m_InvShadowMapResolution = 1.0f / g_GraphicPropertyGrid.m_ShadowControllables.m_ShadowMapResolution;
        basePassConstants.m_CameraOrigin = view.m_Eye;
        basePassConstants.m_SSAOEnabled = g_GraphicPropertyGrid.m_AmbientOcclusionControllables.m_bEnabled;

        memcpy(&basePassConstants.m_CSMDistances, scene->m_CSMSplitDistances, sizeof(basePassConstants.m_CSMDistances));

        for (size_t i = 0; i < Graphic::kNbCSMCascades; i++)
        {
            basePassConstants.m_DirLightViewProj[i] = scene->m_Views[Scene::EView::CSM0 + i].m_ViewProjectionMatrix;
        }

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, basePassConstants);

        // bind and set root signature
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_InstanceConstsBuffer.m_Buffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Graphic.m_VirtualVertexBuffer.m_Buffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, g_Graphic.m_VirtualMeshDataBuffer.m_Buffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Graphic.m_VirtualMaterialDataBuffer.m_Buffer),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler)
        };

        nvrhi::BindingSetHandle bindingSet;
        nvrhi::BindingLayoutHandle bindingLayout;
        g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

        // PSO
        nvrhi::GraphicsPipelineDesc PSODesc;
        PSODesc.VS = g_Graphic.GetShader("basepass_VS_Main");
        PSODesc.PS = params.m_PS;
        PSODesc.renderState = params.m_RenderState;
        PSODesc.inputLayout = g_CommonResources.GPUCullingLayout;
        PSODesc.bindingLayouts = { bindingLayout, g_Graphic.m_BindlessLayout };

        nvrhi::BufferHandle instanceCountBuffer =
            params.m_OverrideInstanceCountBuffer ?
            params.m_OverrideInstanceCountBuffer :
            view.m_InstanceCountBuffer.m_Buffer;

        nvrhi::BufferHandle startInstanceConstsOffsetsBuffer = 
            params.m_OverrideStartInstanceConstsOffsetsBuffer ? 
            params.m_OverrideStartInstanceConstsOffsetsBuffer : 
            view.m_StartInstanceConstsOffsetsBuffer.m_Buffer;

        nvrhi::BufferHandle drawIndexedIndirectArgumentsBuffer = 
            params.m_OverrideDrawIndexedIndirectArgumentsBuffer ? 
            params.m_OverrideDrawIndexedIndirectArgumentsBuffer : 
            view.m_DrawIndexedIndirectArgumentsBuffer.m_Buffer;

        assert(startInstanceConstsOffsetsBuffer);
        assert(drawIndexedIndirectArgumentsBuffer);

        nvrhi::GraphicsState drawState;
        drawState.framebuffer = frameBuffer;
        drawState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)viewportTexDesc.width, (float)viewportTexDesc.height });
        drawState.indexBuffer = { g_Graphic.m_VirtualIndexBuffer.m_Buffer, g_Graphic.m_VirtualIndexBuffer.m_Buffer->getDesc().format, 0 };
        drawState.vertexBuffers = { { startInstanceConstsOffsetsBuffer, 0, 0 } };
        drawState.indirectParams = drawIndexedIndirectArgumentsBuffer;
        drawState.indirectCountBuffer = instanceCountBuffer;
        drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
        drawState.bindings = { bindingSet, g_Graphic.m_DescriptorTableManager->GetDescriptorTable() };

        commandList->setGraphicsState(drawState);

        // NOTE: treating the 2nd arg for 'drawIndexedIndirect' as 'MaxCommandCount' is only legit for d3d12!
        const uint32_t maxCommandCount = scene->m_VisualProxies.size();
        commandList->drawIndexedIndirect(0, maxCommandCount);
    }
};

class OpaqueBasePassRenderer : public BasePassRenderer
{
public:
    OpaqueBasePassRenderer() : BasePassRenderer("OpaqueBasePassRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
    {
        nvrhi::TextureDesc desc;
        desc.width = g_Graphic.m_RenderResolution.x;
        desc.height = g_Graphic.m_RenderResolution.y;
        desc.isRenderTarget = true;
        desc.setClearValue(nvrhi::Color{ 0.0f });
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        desc.format = Graphic::kGBufferAFormat;
        desc.debugName = "GBufferA";
        renderGraph.CreateTransientResource(g_GBufferARDGTextureHandle, desc);

        desc.format = Graphic::kGBufferBFormat;
        desc.debugName = "GBufferB";
        renderGraph.CreateTransientResource(g_GBufferBRDGTextureHandle, desc);

        desc.format = Graphic::kGBufferCFormat;
        desc.debugName = "GBufferC";
        renderGraph.CreateTransientResource(g_GBufferCRDGTextureHandle, desc);

        renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        Scene* scene = g_Graphic.m_Scene.get();

        if (scene->m_VisualProxies.empty())
        {
            return;
        }

        View& view = scene->m_Views[Scene::EView::Main];

        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle GBufferBTexture = renderGraph.GetTexture(g_GBufferBRDGTextureHandle);
        nvrhi::TextureHandle GBufferCTexture = renderGraph.GetTexture(g_GBufferCRDGTextureHandle);
        nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(GBufferATexture);
        frameBufferDesc.addColorAttachment(GBufferBTexture);
        frameBufferDesc.addColorAttachment(GBufferCTexture);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer)
            .depthAttachment.isReadOnly = true;

        RenderBasePassParams params;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main_GBuffer");
        params.m_View = &view;
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthReadStencilNone, g_CommonResources.CullClockwise };
        params.m_FrameBufferDesc = frameBufferDesc;

        RenderBasePass(commandList, params);
    }
};

class TransparentBasePassRenderer : public BasePassRenderer
{
public:
    TransparentBasePassRenderer() : BasePassRenderer("TransparentBasePassRenderer") {}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        // TODO: support transparent
#if 0
        Scene* scene = g_Graphic.m_Scene.get();
        std::vector<uint32_t>& visualProxyIndicesToRender = scene->m_MainViewVisualProxies[Scene::Transparent];

        GPUCullingCounters& cullingCounters = scene->m_GPUCullingCounters[EVisualBucketType::Transparent][Scene::EView::Main];

        // nothing to render. return.
        if (visualProxyIndicesToRender.empty())
        {
            memset(&cullingCounters, 0, sizeof(GPUCullingCounters));
            return;
        }

        m_CommandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST(m_CommandList, m_Name);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(scene->m_LightingOutput);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer)
            .depthAttachment.isReadOnly = true;

        SceneRenderer::Params params;
        params.m_Name = m_Name;
        params.m_CommandList = m_CommandList;
        params.m_VisualProxiesIndicesToRender = visualProxyIndicesToRender;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main");
        params.m_View = &g_Graphic.m_Scene->m_View;
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendAlpha }, g_CommonResources.DepthRead, g_CommonResources.CullClockwise };
        params.m_FrameBufferDesc = frameBufferDesc;

        cullingCounters = m_SceneRenderer.Render(params);
#endif
    }
};

IRenderer* g_SunCSMBasePassRenderers[Graphic::kNbCSMCascades];

class SunCSMBasePassRenderer : public BasePassRenderer
{
    inline static uint32_t ms_CSMIdxCounter = 0;
    const uint32_t m_CSMIndex;

public:
    SunCSMBasePassRenderer()
        : BasePassRenderer(StringFormat("CSM: %d", ms_CSMIdxCounter))
        , m_CSMIndex(ms_CSMIdxCounter++)
    {
        g_SunCSMBasePassRenderers[m_CSMIndex] = this;
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

        if (!shadowControllables.m_bEnabled)
        {
            return false;
        }

        // create shadow map array RDG Texture. CSM0 is responsible for creating it
        if (m_CSMIndex == 0)
        {
            nvrhi::TextureDesc desc;
            desc.width = shadowControllables.m_ShadowMapResolution;
            desc.height = shadowControllables.m_ShadowMapResolution;
            desc.arraySize = Graphic::kNbCSMCascades;
            desc.format = Graphic::kShadowMapFormat;
            desc.dimension = nvrhi::TextureDimension::Texture2DArray;
            desc.debugName = "Shadow Map Array";
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.setClearValue(nvrhi::Color{ Graphic::kFarShadowMapDepth });

            renderGraph.CreateTransientResource(g_ShadowMapArrayRDGTextureHandle, desc);
        }
        else
        {
            // CSMs 1-3 just need to add a write dependency to the shadow map array
            renderGraph.AddWriteDependency(g_ShadowMapArrayRDGTextureHandle);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        Scene* scene = g_Graphic.m_Scene.get();

        if (scene->m_VisualProxies.empty())
        {
            return;
        }

        View& view = scene->m_Views[Scene::EView::CSM0 + m_CSMIndex];

        nvrhi::TextureHandle shadowMapArray = renderGraph.GetTexture(g_ShadowMapArrayRDGTextureHandle);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.setDepthAttachment(shadowMapArray)
            .depthAttachment.setArraySlice(m_CSMIndex);

        nvrhi::DepthStencilState shadowDepthStencilState = g_CommonResources.DepthWriteStencilNone;
        shadowDepthStencilState.depthFunc = Graphic::kInversedShadowMapDepthBuffer ? nvrhi::ComparisonFunc::GreaterOrEqual : nvrhi::ComparisonFunc::LessOrEqual;

        RenderBasePassParams params;
        params.m_View = &scene->m_Views[Scene::EView::CSM0 + m_CSMIndex];
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, shadowDepthStencilState, g_CommonResources.CullCounterClockwise };
        params.m_FrameBufferDesc = frameBufferDesc;

        RenderBasePass(commandList, params);
    }
};

class PickingRenderer : public BasePassRenderer
{
    nvrhi::StagingTextureHandle m_StagingTexture;

public:
    PickingRenderer() : BasePassRenderer("PickingRenderer") {}

    void Initialize() override
    {
        BasePassRenderer::Initialize();

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.format = nvrhi::Format::R32_FLOAT;
        desc.debugName = "Picking staging texture";
        desc.initialState = nvrhi::ResourceStates::CopyDest;
        m_StagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    }

    bool Setup(RenderGraph& renderGraph) override
	{
        Graphic::PickingContext& context = g_Graphic.m_PickingContext;

        // return if picking is in progress
        if (context.m_State == Graphic::PickingContext::NONE || context.m_State == Graphic::PickingContext::RESULT_READY)
        {
            return false;
        }

        if (context.m_State == Graphic::PickingContext::REQUESTED)
        {
            renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);
        }

		return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        Graphic::PickingContext& context = g_Graphic.m_PickingContext;

        // picking in progress & render done last frame. copy result
        if (context.m_State == Graphic::PickingContext::AWAITING_RESULT)
        {
            assert(context.m_RenderTarget);

            // copy result of from staging texture to CPU
            size_t rowPitch = 0;
            void* mapResult = device->mapStagingTexture(m_StagingTexture, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Read, &rowPitch);
            context.m_Result = (uint32_t)*(float*)mapResult;
            device->unmapStagingTexture(m_StagingTexture);

            context.m_RenderTarget.Reset();
            context.m_State = Graphic::PickingContext::RESULT_READY;
        }

        // trigger rendering for picking. also, create picking RT
        else if (context.m_State == Graphic::PickingContext::REQUESTED)
        {
            assert(!context.m_RenderTarget);

            Scene* scene = g_Graphic.m_Scene.get();

            if (scene->m_VisualProxies.empty())
            {
                return;
            }

            View& mainView = scene->m_Views[Scene::EView::Main];

            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;

            // NOTE: uint format just doesnt work for some reason... and we clear to -1 anyway, which is UINT_MAX, which is default for BG pixel
            desc.format = nvrhi::Format::R32_FLOAT;

            desc.debugName = "Picking texture";
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::RenderTarget;
            desc.setClearValue(nvrhi::Color{ -1.0f });
            context.m_RenderTarget = device->createTexture(desc);

            commandList->clearTextureFloat(context.m_RenderTarget, nvrhi::AllSubresources, nvrhi::Color{ -1.0f });

            nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

            nvrhi::FramebufferDesc frameBufferDesc;
            frameBufferDesc.addColorAttachment(context.m_RenderTarget);
            frameBufferDesc.setDepthAttachment(depthStencilBuffer)
                .depthAttachment.isReadOnly = true;

            // render everything to RT. opaque & transparent
            RenderBasePassParams params;
            params.m_PS = g_Graphic.GetShader("basepass_PS_NodeID");
            params.m_View = &mainView;
            params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthReadStencilNone, g_CommonResources.CullClockwise };
            params.m_FrameBufferDesc = frameBufferDesc;

            // render opaque
            RenderBasePass(commandList, params);

            // TODO: support transparent
            // NOTE: dont bother with any alpha blending
            // m_SceneRenderer.Render(params);

            // copy result from RT to staging texture
            commandList->copyTexture(m_StagingTexture, nvrhi::TextureSlice{}, context.m_RenderTarget, nvrhi::TextureSlice{ context.m_PickingLocation.x, context.m_PickingLocation.y, 0, 1, 1, 1 });

            context.m_State = Graphic::PickingContext::AWAITING_RESULT;
        }
    }
};

IRenderer* g_GPUCullingRenderer[EnumUtils::Count<Scene::EView>()];

class GPUCullingRenderer : public BasePassRenderer
{
    inline static uint32_t ms_ViewIdxCounter = 0;
    const uint32_t m_ViewIndex;
    const bool m_bIsMainView; // NOTE: Renderers & Scene views init in parallel... so View's 'm_bIsMainView' might not be properly initialized

    FFXHelpers::SPD m_SPDHelper;

    RenderGraph::ResourceHandle m_CounterStatsRDGBufferHandle;
    RenderGraph::ResourceHandle m_PhaseTwoCullingIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_PhaseTwoInstanceIndexCounterRDGBufferHandle;

    FencedReadbackBuffer m_CounterStatsReadbackBuffer;
    SimpleResizeableGPUBuffer m_PhaseTwoCullingIndicesBuffer;

public:
    GPUCullingRenderer()
        : BasePassRenderer(StringFormat("GPUCulling: %s", EnumUtils::ToString((Scene::EView)ms_ViewIdxCounter)))
        , m_ViewIndex(ms_ViewIdxCounter++)
        , m_bIsMainView(m_ViewIndex == (uint32_t)Scene::EView::Main)
    {
        g_GPUCullingRenderer[m_ViewIndex] = this;
    }

    void Initialize() override
    {
        // we only need the BasePass functionality for main view for populating depth buffer
        if (m_bIsMainView)
        {
            BasePassRenderer::Initialize();
        }

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        m_CounterStatsReadbackBuffer.Initialize(device, sizeof(uint32_t) * kNbGPUCullingBufferCounters);

        if (m_bIsMainView)
        {
            m_PhaseTwoCullingIndicesBuffer.m_BufferDesc.structStride = sizeof(uint32_t);
            m_PhaseTwoCullingIndicesBuffer.m_BufferDesc.debugName = "PhaseTwoCullingIndicesBuffer";
            m_PhaseTwoCullingIndicesBuffer.m_BufferDesc.canHaveUAVs = true;
            m_PhaseTwoCullingIndicesBuffer.m_BufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;

            nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
            SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "GPUCullingRenderer Init SPDHelper");
        }
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(uint32_t) * kNbGPUCullingBufferCounters;
            desc.structStride = sizeof(uint32_t);
            desc.canHaveUAVs = true;
            desc.debugName = "GPUCullingCounterStats";
            desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            renderGraph.CreateTransientResource(m_CounterStatsRDGBufferHandle, desc);
        }

        if (m_bIsMainView)
        {
            m_SPDHelper.CreateTransientResources(renderGraph);

            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.format = Graphic::kDepthStencilFormat;
            desc.debugName = "Depth Buffer";
            desc.isRenderTarget = true;
            desc.setClearValue(nvrhi::Color{ Graphic::kFarDepth, Graphic::kStencilBit_Sky, 0.0f, 0.0f });
            desc.initialState = nvrhi::ResourceStates::DepthRead;
            renderGraph.CreateTransientResource(g_DepthStencilBufferRDGTextureHandle, desc);

            desc.format = Graphic::kDepthBufferCopyFormat;
            desc.debugName = "Depth Buffer Copy";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(g_DepthBufferCopyRDGTextureHandle, desc);

            {
                nvrhi::BufferDesc desc;
                desc.byteSize = sizeof(DispatchIndirectArguments);
                desc.structStride = sizeof(DispatchIndirectArguments);
                desc.debugName = "PhaseTwoCullingIndirectArgsBuffer";
                desc.canHaveUAVs = true;
                desc.isDrawIndirectArgs = true;
                desc.initialState = nvrhi::ResourceStates::IndirectArgument;
                renderGraph.CreateTransientResource(m_PhaseTwoCullingIndirectArgsRDGBufferHandle, desc);
            }
            {
                nvrhi::BufferDesc desc;
                desc.byteSize = sizeof(uint32_t);
                desc.structStride = sizeof(uint32_t);
                desc.canHaveUAVs = true;
                desc.debugName = "PhaseTwoInstanceIndexCounter";
                desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
                renderGraph.CreateTransientResource(m_PhaseTwoInstanceIndexCounterRDGBufferHandle, desc);
            }
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();

		const uint32_t nbInstances = scene->m_VisualProxies.size();
        if (nbInstances == 0)
        {
            return;
        }

        View& view = scene->m_Views[m_ViewIndex];

        GraphicPropertyGrid::DebugControllables& debugControllables = g_GraphicPropertyGrid.m_DebugControllables;

        // read back nb visible instances from counter
        {
            uint32_t readbackResults[kNbGPUCullingBufferCounters]{};
            m_CounterStatsReadbackBuffer.Read(device, readbackResults);

            // TODO: support transparent
            GPUCullingCounters& cullingCounters = view.m_GPUCullingCounters;
            cullingCounters.m_Frustum = readbackResults[kFrustumCullingBufferCounterIdx];
            cullingCounters.m_OcclusionPhase1 = readbackResults[kOcclusionCullingPhase1BufferCounterIdx];
            cullingCounters.m_OcclusionPhase2 = readbackResults[kOcclusionCullingPhase2BufferCounterIdx];
        }

        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);

        nvrhi::TextureHandle depthStencilBuffer;
        nvrhi::BufferHandle phaseTwoInstanceIndexCounter;
        nvrhi::BufferHandle phaseTwoCullingIndirectArgsBuffer;
        if (m_bIsMainView)
        {
            depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
            phaseTwoInstanceIndexCounter = renderGraph.GetBuffer(m_PhaseTwoInstanceIndexCounterRDGBufferHandle);
            phaseTwoCullingIndirectArgsBuffer = renderGraph.GetBuffer(m_PhaseTwoCullingIndirectArgsRDGBufferHandle);
        }

        {
            PROFILE_SCOPED("Clear Buffers");
            PROFILE_GPU_SCOPED(commandList, "Clear Buffers");

            view.m_InstanceCountBuffer.ClearBuffer(commandList, nbInstances * sizeof(uint32_t));
            view.m_StartInstanceConstsOffsetsBuffer.ClearBuffer(commandList, nbInstances * sizeof(uint32_t));
            view.m_DrawIndexedIndirectArgumentsBuffer.ClearBuffer(commandList, nbInstances * sizeof(DrawIndexedIndirectArguments));
            commandList->clearBufferUInt(counterStatsBuffer, 0);

            if (m_bIsMainView)
            {
                m_PhaseTwoCullingIndicesBuffer.ClearBuffer(commandList, sizeof(uint32_t) * nbInstances);

                commandList->clearBufferUInt(phaseTwoInstanceIndexCounter, 0);
                scene->m_OcclusionCullingPhaseTwoInstanceCountBuffer.ClearBuffer(commandList, nbInstances * sizeof(uint32_t));
                scene->m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.ClearBuffer(commandList, nbInstances * sizeof(uint32_t));
                scene->m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.ClearBuffer(commandList, nbInstances * sizeof(DrawIndexedIndirectArguments));
            }
        }

        // Process instances and output meshlets of each visible instance.
        // In Phase 1, also output instances which are occluded according to the previous frame's HZB, and have to be retested in Phase 2.
        // In Phase 2, outputs visible instances which were considered occluded before, but are not based on the updated HZB created in Phase 1.
        const bool bDoOcclusionCulling = debugControllables.m_bEnableGPUOcclusionCulling;

        auto DoGPUCulling = [&](bool bIsFirstPhase)
            {
                const char* passName = "";

                if (view.m_bIsMainView)
                {
                    passName = bIsFirstPhase ? "1st Phase Culling" : "2nd Phase Culling";
                }
                else
                {
                    passName = "GPU Frustum Culling";
                }

                PROFILE_SCOPED(passName);
                PROFILE_GPU_SCOPED(commandList, passName);

                uint32_t occlusionCullingFlags = 0;

                // only perform occlusion culling for main view due to usage of HZB
                if (m_bIsMainView)
                {
                    occlusionCullingFlags |= bDoOcclusionCulling ? OcclusionCullingFlag_Enable : 0;
                    occlusionCullingFlags |= (bDoOcclusionCulling && bIsFirstPhase) ? OcclusionCullingFlag_IsFirstPhase : 0;
                }

                const nvrhi::TextureDesc& HZBDesc = scene->m_HZB->getDesc();

                GPUCullingPassConstants passParameters{};
                passParameters.m_NbInstances = bIsFirstPhase ? nbInstances : 0; // 2nd phase nb instances is driven by indirect buffer
                passParameters.m_EnableFrustumCulling = bIsFirstPhase ? debugControllables.m_bEnableGPUFrustumCulling : false; // 2nd phase instances are already frustum culled
                passParameters.m_OcclusionCullingFlags = occlusionCullingFlags;
                passParameters.m_WorldToClip = view.m_ViewProjectionMatrix;
                passParameters.m_PrevFrameWorldToClip = view.m_PrevFrameViewProjectionMatrix;
                passParameters.m_HZBDimensions = Vector2U{ HZBDesc.width, HZBDesc.height };
                passParameters.m_HZBMipCount = HZBDesc.mipLevels;

                nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passParameters);

                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_InstanceConstsBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Graphic.m_VirtualMeshDataBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(2, bIsFirstPhase ? g_CommonResources.DummyUintStructuredBuffer : m_PhaseTwoCullingIndicesBuffer.m_Buffer),
                    nvrhi::BindingSetItem::Texture_SRV(3, scene->m_HZB),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(4, bIsFirstPhase ? g_CommonResources.DummyUintStructuredBuffer : scene->m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(0, view.m_DrawIndexedIndirectArgumentsBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(1, view.m_StartInstanceConstsOffsetsBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(2, view.m_InstanceCountBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(3, counterStatsBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(4, (bIsFirstPhase && m_bIsMainView) ? m_PhaseTwoCullingIndicesBuffer.m_Buffer : g_CommonResources.DummyUintStructuredBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(5, (bIsFirstPhase && m_bIsMainView) ? scene->m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_Buffer : g_CommonResources.DummyUintStructuredBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(6, bIsFirstPhase ? g_CommonResources.DummyUintStructuredBuffer : scene->m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(7, bIsFirstPhase ? g_CommonResources.DummyUintStructuredBuffer : scene->m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.m_Buffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(8, bIsFirstPhase ? g_CommonResources.DummyUintStructuredBuffer : phaseTwoInstanceIndexCounter),
                    nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
                };

                if (bIsFirstPhase)
                {
                    const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(nbInstances, kNbGPUCullingGroupThreads);
                    g_Graphic.AddComputePass(commandList, "gpuculling_CS_GPUCulling", bindingSetDesc, dispatchGroupSize);
                }
                else
                {
                    g_Graphic.AddComputePass(commandList, "gpuculling_CS_GPUCulling", bindingSetDesc, phaseTwoCullingIndirectArgsBuffer);
                }
            };

        auto PopulateDepthBuffer = [&](bool bIsFirstPhase)
            {
                const char* passName = bIsFirstPhase ? "1st Phase Depth Buffer" : "2nd Phase Depth Buffer";

                PROFILE_SCOPED(passName);
                PROFILE_GPU_SCOPED(commandList, passName);

                nvrhi::FramebufferDesc frameBufferDesc;
                frameBufferDesc.setDepthAttachment(depthStencilBuffer);

                nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthWriteStencilWrite;
                depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
                depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;

                RenderBasePassParams params;
                params.m_View = &view;
                params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, depthStencilState, g_CommonResources.CullClockwise };
                params.m_FrameBufferDesc = frameBufferDesc;

                if (!bIsFirstPhase)
                {
                    params.m_OverrideInstanceCountBuffer = scene->m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_Buffer;
                    params.m_OverrideStartInstanceConstsOffsetsBuffer = scene->m_OcclusionCullingPhaseTwoStartInstanceConstsOffsetsBuffer.m_Buffer;
                    params.m_OverrideDrawIndexedIndirectArgumentsBuffer = scene->m_OcclusionCullingPhaseTwoDrawIndexedIndirectArgumentsBuffer.m_Buffer;
                }

                RenderBasePass(commandList, params);
            };

        auto GenerateHZB = [&](std::string_view passName)
            {
                PROFILE_SCOPED(passName.data());

                const nvrhi::TextureDesc& hzbDesc = scene->m_HZB->getDesc();

                MinMaxDownsampleConsts PassParameters;
                PassParameters.m_OutputDimensions = Vector2U{ hzbDesc.width, hzbDesc.height };
                PassParameters.m_bDownsampleMax = false;

                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::PushConstants(0, sizeof(PassParameters)),
                    nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer),
                    nvrhi::BindingSetItem::Texture_UAV(0, scene->m_HZB),
                    nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
                };

                const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector2U{ hzbDesc.width, hzbDesc.height }, 8);
                g_Graphic.AddComputePass(commandList, "minmaxdownsample_CS_Main", bindingSetDesc, dispatchGroupSize, &PassParameters, sizeof(PassParameters));

                // generate HZB mip chain
                const nvrhi::SamplerReductionType reductionType = Graphic::kInversedDepthBuffer ? nvrhi::SamplerReductionType::Minimum : nvrhi::SamplerReductionType::Maximum;
                m_SPDHelper.Execute(commandList, renderGraph, depthStencilBuffer, scene->m_HZB, reductionType);
            };

        // 1st phase
        bool bIsFirstPhase = true;
        DoGPUCulling(bIsFirstPhase);

        if (m_bIsMainView)
        {
            // populate Depth Buffer with instances that passed the 1st phase culling
            PopulateDepthBuffer(bIsFirstPhase);

            if (bDoOcclusionCulling)
            {
                GenerateHZB("1st Phase HZB");

                // prepare for 2nd phase culling
                {
                    PROFILE_SCOPED("Prepare 2nd Phase Indirect Args & Buffers");
                    PROFILE_GPU_SCOPED(commandList, "Prepare 2nd Phase Indirect Args & Buffers");

                    nvrhi::BindingSetDesc bindingSetDesc;
                    bindingSetDesc.bindings = {
                        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_OcclusionCullingPhaseTwoInstanceCountBuffer.m_Buffer),
                        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, phaseTwoCullingIndirectArgsBuffer),
                    };

                    g_Graphic.AddComputePass(commandList, "gpuculling_CS_BuildPhaseTwoIndirectArgs", bindingSetDesc, Vector3U{ 1, 1, 1 });
                }

                // 2nd Phase culling
                bIsFirstPhase = false;
                DoGPUCulling(bIsFirstPhase);

                // populate Depth Buffer again with instances that passed the 2nd phase
                PopulateDepthBuffer(bIsFirstPhase);

                GenerateHZB("2nd Phase HZB");
            }

            // at this point, we have the final depth buffer. create a copy for SRV purposes
            {
                PROFILE_GPU_SCOPED(commandList, "Copy depth buffer");

                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer),
                };

                nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

                nvrhi::FramebufferDesc frameBufferDesc;
                frameBufferDesc.addColorAttachment(depthBufferCopy);

                g_Graphic.AddFullScreenPass(commandList, frameBufferDesc, bindingSetDesc, "fullscreen_PS_Passthrough");
            }
        }

        // copy counter buffer, so that it can be read on CPU next frame
        m_CounterStatsReadbackBuffer.CopyTo(device, commandList, counterStatsBuffer);
    }
};

static OpaqueBasePassRenderer gs_OpaqueBasePassRenderer;
IRenderer* g_OpaqueBasePassRenderer = &gs_OpaqueBasePassRenderer;

static TransparentBasePassRenderer gs_TransparentBasePassRenderer;
IRenderer* g_TransparentBasePassRenderer = &gs_TransparentBasePassRenderer;

static SunCSMBasePassRenderer gs_CSMRenderers[Graphic::kNbCSMCascades];

static GPUCullingRenderer gs_GPUCullingRenderers[EnumUtils::Count<Scene::EView>()];

static PickingRenderer gs_PickingRenderer;
IRenderer* g_PickingRenderer = &gs_PickingRenderer;
