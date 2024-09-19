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
    struct CullingBuffersRDG
    {
        RenderGraph::ResourceHandle m_InstanceCountBuffer;
        RenderGraph::ResourceHandle m_DrawIndexedIndirectArgumentsBuffer;
        RenderGraph::ResourceHandle m_StartInstanceConstsOffsetsBuffer;
    };

    struct CullingBuffers
    {
        nvrhi::BufferHandle m_InstanceCountBuffer;
        nvrhi::BufferHandle m_DrawIndexedIndirectArgumentsBuffer;
        nvrhi::BufferHandle m_StartInstanceConstsOffsetsBuffer;
    };

    struct RenderBasePassParams
    {
        nvrhi::ShaderHandle m_PS;
        View* m_View;
        nvrhi::RenderState m_RenderState;
        nvrhi::FramebufferDesc m_FrameBufferDesc;
    };

    FFXHelpers::SPD m_SPDHelper;
    CullingBuffersRDG m_CullingBuffersRDG[2];
    FencedReadbackBuffer m_CounterStatsReadbackBuffer;
    RenderGraph::ResourceHandle m_CounterStatsRDGBufferHandle;

    BasePassRenderer(const char* rendererName) : IRenderer(rendererName) {}

    void Initialize() override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        m_CounterStatsReadbackBuffer.Initialize(device, sizeof(uint32_t) * kNbGPUCullingBufferCounters);
    }

	bool Setup(RenderGraph& renderGraph) override
	{
        m_SPDHelper.CreateTransientResources(renderGraph);

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(uint32_t) * kNbGPUCullingBufferCounters;
            desc.structStride = sizeof(uint32_t);
            desc.canHaveUAVs = true;
            desc.debugName = "GPUCullingCounterStats";
            desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            renderGraph.CreateTransientResource(m_CounterStatsRDGBufferHandle, desc);
		}

		const uint32_t nbInstances = g_Graphic.m_Scene->m_VisualProxies.size();
		if (nbInstances > 0)
		{
			for (uint32_t i = 0; i < 2; ++i)
			{
				{
					nvrhi::BufferDesc desc;
					desc.byteSize = sizeof(uint32_t) * nbInstances;
					desc.structStride = sizeof(uint32_t);
					desc.canHaveUAVs = true;
					desc.isDrawIndirectArgs = true;
					desc.initialState = nvrhi::ResourceStates::ShaderResource;
					desc.debugName = StringFormat("InstanceIndexCounter %d", i);

					renderGraph.CreateTransientResource(m_CullingBuffersRDG[i].m_InstanceCountBuffer, desc);
				}
				{
					nvrhi::BufferDesc desc;
					desc.byteSize = sizeof(DrawIndexedIndirectArguments) * nbInstances;
					desc.structStride = sizeof(DrawIndexedIndirectArguments);
					desc.canHaveUAVs = true;
					desc.isDrawIndirectArgs = true;
					desc.initialState = nvrhi::ResourceStates::IndirectArgument;
					desc.debugName = StringFormat("DrawIndexedIndirectArguments %d", i);

					renderGraph.CreateTransientResource(m_CullingBuffersRDG[i].m_DrawIndexedIndirectArgumentsBuffer, desc);
				}

				{
					nvrhi::BufferDesc desc;
					desc.byteSize = sizeof(uint32_t) * nbInstances;
					desc.structStride = sizeof(uint32_t);
					desc.canHaveUAVs = true;
					desc.isVertexBuffer = true;
					desc.initialState = nvrhi::ResourceStates::VertexBuffer;
					desc.debugName = StringFormat("StartInstanceConstsOffsets %d", i);

					renderGraph.CreateTransientResource(m_CullingBuffersRDG[i].m_StartInstanceConstsOffsetsBuffer, desc);
				}
			}
		}

		return true;
	}

    void GPUCulling(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params, nvrhi::BufferHandle counterStatsBuffer, const CullingBuffers cullingBuffers[2])
    {
        PROFILE_FUNCTION();

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& view = *params.m_View;

        const uint32_t nbInstances = scene->m_VisualProxies.size();
        assert(nbInstances > 0);

        {
            PROFILE_GPU_SCOPED(commandList, "Clear Buffers");

            commandList->clearBufferUInt(counterStatsBuffer, 0);

            for (uint32_t i = 0; i < 2; ++i)
            {
				commandList->clearBufferUInt(cullingBuffers[i].m_InstanceCountBuffer, 0);
				commandList->clearBufferUInt(cullingBuffers[i].m_StartInstanceConstsOffsetsBuffer, 0);
				commandList->clearBufferUInt(cullingBuffers[i].m_DrawIndexedIndirectArgumentsBuffer, 0);
            }
        }

        const auto& debugControllables = g_GraphicPropertyGrid.m_DebugControllables;

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

        GPUCullingPassConstants passParameters{};
        passParameters.m_NbInstances = nbInstances;
        passParameters.m_EnableFrustumCulling = debugControllables.m_bEnableGPUFrustumCulling;
        passParameters.m_OcclusionCullingFlags = 0;
        passParameters.m_WorldToClip = view.m_ViewProjectionMatrix;
        passParameters.m_PrevFrameWorldToClip = view.m_PrevFrameViewProjectionMatrix;

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passParameters);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_InstanceConstsBuffer.m_Buffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Graphic.m_VirtualMeshDataBuffer.m_Buffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, cullingBuffers[0].m_DrawIndexedIndirectArgumentsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(1, cullingBuffers[0].m_StartInstanceConstsOffsetsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(2, cullingBuffers[0].m_InstanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(3, counterStatsBuffer),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
        };

        const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(nbInstances, kNbGPUCullingGroupThreads);
        g_Graphic.AddComputePass(commandList, "gpuculling_CS_GPUCulling", bindingSetDesc, dispatchGroupSize);
    }

    void RenderInstances(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params, const CullingBuffers cullingBuffers[2])
    {
        PROFILE_FUNCTION();

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& view = *params.m_View;

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

        nvrhi::GraphicsState drawState;
        drawState.framebuffer = frameBuffer;
        drawState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)viewportTexDesc.width, (float)viewportTexDesc.height });
        drawState.indexBuffer = { g_Graphic.m_VirtualIndexBuffer.m_Buffer, g_Graphic.m_VirtualIndexBuffer.m_Buffer->getDesc().format, 0 };
        drawState.vertexBuffers = { { cullingBuffers[0].m_StartInstanceConstsOffsetsBuffer, 0, 0} };
        drawState.indirectParams = cullingBuffers[0].m_DrawIndexedIndirectArgumentsBuffer;
        drawState.indirectCountBuffer = cullingBuffers[0].m_InstanceCountBuffer;
        drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
        drawState.bindings = { bindingSet, g_Graphic.m_DescriptorTableManager->GetDescriptorTable() };

        commandList->setGraphicsState(drawState);

        // NOTE: treating the 2nd arg for 'drawIndexedIndirect' as 'MaxCommandCount' is only legit for d3d12!
        const uint32_t maxCommandCount = scene->m_VisualProxies.size();
        commandList->drawIndexedIndirect(0, maxCommandCount);
    }

    void RenderBasePass(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        assert(params.m_View);

        // early cull: frustum cull & fill objects that *were* visible last frame
        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);
        CullingBuffers cullingBuffers[2];

        for (uint32_t i = 0; i < 2; ++i)
        {
            cullingBuffers[i].m_InstanceCountBuffer = renderGraph.GetBuffer(m_CullingBuffersRDG[i].m_InstanceCountBuffer);
            cullingBuffers[i].m_DrawIndexedIndirectArgumentsBuffer = renderGraph.GetBuffer(m_CullingBuffersRDG[i].m_DrawIndexedIndirectArgumentsBuffer);
            cullingBuffers[i].m_StartInstanceConstsOffsetsBuffer = renderGraph.GetBuffer(m_CullingBuffersRDG[i].m_StartInstanceConstsOffsetsBuffer);
        }

        GPUCulling(commandList, renderGraph, params, counterStatsBuffer, cullingBuffers);

        // early render: render objects that were visible last frame
        RenderInstances(commandList, renderGraph, params, cullingBuffers);

        // depth pyramid generation
        //pyramid();

        // late cull: frustum + occlusion cull & fill objects that were *not* visible last frame
        //cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 6, "late cull", /* late= */ true);

        // late render: render objects that are visible this frame but weren't drawn in the early pass
        //render(/* late= */ true, colorClear, depthClear, 1, 10, "late render");

        // copy counter buffer, so that it can be read on CPU next frame
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        m_CounterStatsReadbackBuffer.CopyTo(device, commandList, counterStatsBuffer);
    }
};

class GBufferRenderer : public BasePassRenderer
{
public:
    GBufferRenderer() : BasePassRenderer("GBufferRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
    {
		BasePassRenderer::Setup(renderGraph);

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

        {
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

        View& view = scene->m_Views[Scene::EView::Main];

        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle GBufferBTexture = renderGraph.GetTexture(g_GBufferBRDGTextureHandle);
        nvrhi::TextureHandle GBufferCTexture = renderGraph.GetTexture(g_GBufferCRDGTextureHandle);
        nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(GBufferATexture);
        frameBufferDesc.addColorAttachment(GBufferBTexture);
        frameBufferDesc.addColorAttachment(GBufferCTexture);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer);

        // write 'opaque' to stencil buffer
        nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthWriteStencilWrite;
        depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
        depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;

        RenderBasePassParams params;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main_GBuffer");
        params.m_View = &view;
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, depthStencilState, g_CommonResources.CullClockwise };
        params.m_FrameBufferDesc = frameBufferDesc;

        RenderBasePass(commandList, renderGraph, params);

        // at this point, we have the final depth buffer. create a copy for SRV purposes
        {
            PROFILE_GPU_SCOPED(commandList, "Copy depth buffer");

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = { nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer), };

            nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

            nvrhi::FramebufferDesc frameBufferDesc;
            frameBufferDesc.addColorAttachment(depthBufferCopy);

            g_Graphic.AddFullScreenPass(commandList, frameBufferDesc, bindingSetDesc, "fullscreen_PS_Passthrough");
        }
    }
};

class TransparentBasePassRenderer : public BasePassRenderer
{
public:
    TransparentBasePassRenderer() : BasePassRenderer("TransparentBasePassRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
	{
        // TODO
        return false;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        // TODO
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

        RenderBasePass(commandList, renderGraph, params);
    }
};

static GBufferRenderer gs_GBufferRenderer;
IRenderer* g_GBufferRenderer = &gs_GBufferRenderer;

static TransparentBasePassRenderer gs_TransparentBasePassRenderer;
IRenderer* g_TransparentBasePassRenderer = &gs_TransparentBasePassRenderer;

static SunCSMBasePassRenderer gs_CSMRenderers[Graphic::kNbCSMCascades];
