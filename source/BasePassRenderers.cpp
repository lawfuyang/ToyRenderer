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
static_assert(sizeof(DrawIndexedIndirectArguments) == sizeof(nvrhi::DrawIndexedIndirectArguments));

static_assert(SamplerIdx_AnisotropicClamp == (int)nvrhi::SamplerAddressMode::Clamp);
static_assert(SamplerIdx_AnisotropicWrap == (int)nvrhi::SamplerAddressMode::Wrap);
static_assert(SamplerIdx_AnisotropicBorder == (int)nvrhi::SamplerAddressMode::Border);
static_assert(SamplerIdx_AnisotropicMirror == (int)nvrhi::SamplerAddressMode::Mirror);

RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
RenderGraph::ResourceHandle g_ShadowMapArrayRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class BasePassRenderer : public IRenderer
{
    RenderGraph::ResourceHandle m_InstanceCountRDGBufferHandle;
    RenderGraph::ResourceHandle m_DrawIndexedIndirectArgumentsRDGBufferHandle;
    RenderGraph::ResourceHandle m_StartInstanceConstsOffsetsRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullDispatchIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullInstanceCountBufferRDGBufferHandle;
    RenderGraph::ResourceHandle m_lateCullInstanceIDsBufferRDGBufferHandle;

    FFXHelpers::SPD m_SPDHelper;
    FencedReadbackBuffer m_CounterStatsReadbackBuffer;
    RenderGraph::ResourceHandle m_CounterStatsRDGBufferHandle;

    const bool m_bDoOcclusionCulling;

public:
    struct RenderBasePassParams
    {
        nvrhi::ShaderHandle m_PS;
        nvrhi::ShaderHandle m_PSAlphaMask;
        View* m_View;
        nvrhi::RenderState m_RenderState;
        nvrhi::FramebufferDesc m_FrameBufferDesc;
    };

    BasePassRenderer(const char* rendererName, bool bDoOcclusionCulling)
        : IRenderer(rendererName)
		, m_bDoOcclusionCulling(bDoOcclusionCulling)
    {}

	void Initialize() override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		m_CounterStatsReadbackBuffer.Initialize(device, sizeof(uint32_t) * kNbGPUCullingBufferCounters);
	}

	bool Setup(RenderGraph& renderGraph) override
	{
		const uint32_t nbInstances = g_Graphic.m_Scene->m_Primitives.size();
        if (nbInstances == 0)
        {
            return true;
		}

		{
			nvrhi::BufferDesc desc;
			desc.byteSize = sizeof(uint32_t) * kNbGPUCullingBufferCounters;
			desc.structStride = sizeof(uint32_t);
			desc.canHaveUAVs = true;
			desc.debugName = "GPUCullingCounterStats";
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			renderGraph.CreateTransientResource(m_CounterStatsRDGBufferHandle, desc);
		}

		{
			nvrhi::BufferDesc desc;
			desc.byteSize = sizeof(uint32_t);
			desc.structStride = sizeof(uint32_t);
			desc.canHaveUAVs = true;
			desc.isDrawIndirectArgs = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.debugName = "InstanceIndexCounter";

			renderGraph.CreateTransientResource(m_InstanceCountRDGBufferHandle, desc);
		}
		{
			nvrhi::BufferDesc desc;
			desc.byteSize = sizeof(DrawIndexedIndirectArguments) * nbInstances;
			desc.structStride = sizeof(DrawIndexedIndirectArguments);
			desc.canHaveUAVs = true;
			desc.isDrawIndirectArgs = true;
			desc.initialState = nvrhi::ResourceStates::IndirectArgument;
			desc.debugName = "DrawIndexedIndirectArguments";

			renderGraph.CreateTransientResource(m_DrawIndexedIndirectArgumentsRDGBufferHandle, desc);
		}

		{
			nvrhi::BufferDesc desc;
			desc.byteSize = sizeof(uint32_t) * nbInstances;
			desc.structStride = sizeof(uint32_t);
			desc.canHaveUAVs = true;
			desc.isVertexBuffer = true;
			desc.initialState = nvrhi::ResourceStates::VertexBuffer;
			desc.debugName = "StartInstanceConstsOffsets";

			renderGraph.CreateTransientResource(m_StartInstanceConstsOffsetsRDGBufferHandle, desc);
		}

        const auto& controllables = g_GraphicPropertyGrid.m_InstanceRenderingControllables;
        const bool bDoOcclusionCulling = controllables.m_bEnableOcclusionCulling && m_bDoOcclusionCulling;

		if (bDoOcclusionCulling)
		{
			m_SPDHelper.CreateTransientResources(renderGraph);

			{
				nvrhi::BufferDesc desc;
				desc.byteSize = sizeof(DispatchIndirectArguments);
				desc.structStride = sizeof(DispatchIndirectArguments);
				desc.canHaveUAVs = true;
				desc.isDrawIndirectArgs = true;
				desc.initialState = nvrhi::ResourceStates::IndirectArgument;
				desc.debugName = "LateCullDispatchIndirectArgs";

				renderGraph.CreateTransientResource(m_LateCullDispatchIndirectArgsRDGBufferHandle, desc);
			}

			{
				nvrhi::BufferDesc desc;
				desc.byteSize = sizeof(uint32_t);
				desc.structStride = sizeof(uint32_t);
				desc.canHaveUAVs = true;
				desc.initialState = nvrhi::ResourceStates::ShaderResource;
				desc.debugName = "LateCullInstanceCountBuffer";

				renderGraph.CreateTransientResource(m_LateCullInstanceCountBufferRDGBufferHandle, desc);
			}

            {
				nvrhi::BufferDesc desc;
				desc.byteSize = sizeof(uint32_t) * nbInstances;
				desc.structStride = sizeof(uint32_t);
				desc.canHaveUAVs = true;
                desc.initialState = nvrhi::ResourceStates::ShaderResource;
				desc.debugName = "LateCullInstanceIDsBuffer";

				renderGraph.CreateTransientResource(m_lateCullInstanceIDsBufferRDGBufferHandle, desc);
            }
		}

		return true;
	}

    void GPUCulling(
        nvrhi::CommandListHandle commandList,
        const RenderGraph& renderGraph,
        const RenderBasePassParams& params,
        bool bLateCull,
        bool bAlphaMaskPrimitives)
    {
        PROFILE_FUNCTION();
        PROFILE_GPU_SCOPED(commandList, "GPU Culling");

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& view = *params.m_View;
        const auto& controllables = g_GraphicPropertyGrid.m_InstanceRenderingControllables;

        const uint32_t nbInstances = bAlphaMaskPrimitives ? scene->m_AlphaMaskPrimitiveIDs.size() : scene->m_OpaquePrimitiveIDs.size();

        if (nbInstances == 0)
        {
            return;
        }

        const bool bDoOcclusionCulling = controllables.m_bEnableOcclusionCulling && m_bDoOcclusionCulling;

        nvrhi::BufferHandle instanceCountBuffer = renderGraph.GetBuffer(m_InstanceCountRDGBufferHandle);
        nvrhi::BufferHandle drawIndexedIndirectArgumentsBuffer = renderGraph.GetBuffer(m_DrawIndexedIndirectArgumentsRDGBufferHandle);
        nvrhi::BufferHandle startInstanceConstsOffsetsBuffer = renderGraph.GetBuffer(m_StartInstanceConstsOffsetsRDGBufferHandle);
        nvrhi::BufferHandle lateCullDispatchIndirectArgsBuffer = bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullDispatchIndirectArgsRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
        nvrhi::BufferHandle lateCullInstanceCountBuffer = bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullInstanceCountBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
		nvrhi::BufferHandle lateCullInstanceIDsBuffer = bDoOcclusionCulling ? renderGraph.GetBuffer(m_lateCullInstanceIDsBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);

        {
            PROFILE_GPU_SCOPED(commandList, "Clear Buffers");

			commandList->clearBufferUInt(instanceCountBuffer, 0);
			commandList->clearBufferUInt(startInstanceConstsOffsetsBuffer, 0);
			commandList->clearBufferUInt(drawIndexedIndirectArgumentsBuffer, 0);

            if (!bLateCull && bDoOcclusionCulling)
            {
                commandList->clearBufferUInt(lateCullInstanceCountBuffer, 0);
				commandList->clearBufferUInt(lateCullInstanceIDsBuffer, 0);
            }
        }

        // read back nb visible instances from counter
        {
            uint32_t readbackResults[kNbGPUCullingBufferCounters]{};
            m_CounterStatsReadbackBuffer.Read(device, readbackResults);

            // TODO: support transparent
            GPUCullingCounters& cullingCounters = view.m_GPUCullingCounters;
            cullingCounters.m_Early = readbackResults[kCullingEarlyBufferCounterIdx];
            cullingCounters.m_Late = readbackResults[kCullingLateBufferCounterIdx];
        }

        uint32_t flags = controllables.m_bEnableFrustumCulling ? CullingFlag_FrustumCullingEnable : 0;
        flags |= bDoOcclusionCulling ? CullingFlag_OcclusionCullingEnable : 0;

        const Vector2U HZBDims = bDoOcclusionCulling ? Vector2U{ scene->m_HZB->getDesc().width, scene->m_HZB->getDesc().height } : Vector2U{ 1, 1 };

        Matrix projectionT = view.m_ProjectionMatrix.Transpose();
        Vector4 frustumX = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[0] };
        Vector4 frustumY = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[1] };
        frustumX.Normalize();
        frustumY.Normalize();

        GPUCullingPassConstants passParameters{};
        passParameters.m_NbInstances = nbInstances;
        passParameters.m_Flags = flags;
        passParameters.m_Frustum.x = frustumX.x;
        passParameters.m_Frustum.y = frustumX.z;
        passParameters.m_Frustum.z = frustumY.y;
        passParameters.m_Frustum.w = frustumY.z;
        passParameters.m_HZBDimensions = HZBDims;
        passParameters.m_ViewMatrix = view.m_ViewMatrix;
        passParameters.m_PrevViewMatrix = view.m_PrevFrameViewMatrix;
        passParameters.m_NearPlane = view.m_ZNearP;
        passParameters.m_P00 = view.m_ProjectionMatrix.m[0][0];
        passParameters.m_P11 = view.m_ProjectionMatrix.m[1][1];

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passParameters);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, bAlphaMaskPrimitives ? scene->m_AlphaMaskInstanceIDsBuffer : scene->m_OpaqueInstanceIDsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(3, bDoOcclusionCulling ? scene->m_HZB : g_CommonResources.BlackTexture.m_NVRHITextureHandle),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, drawIndexedIndirectArgumentsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(1, startInstanceConstsOffsetsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(2, instanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(3, counterStatsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(4, lateCullInstanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(5, lateCullInstanceIDsBuffer),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampMinReductionSampler)
        };

        const std::string shaderName = StringFormat("gpuculling_CS_GPUCulling LATE=%d", bLateCull ? 1 : 0);

        if (!bLateCull)
        {
            g_Graphic.AddComputePass(commandList, shaderName, bindingSetDesc, ComputeShaderUtils::GetGroupCount(nbInstances, kNbGPUCullingGroupThreads));

            if (bDoOcclusionCulling)
            {
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, lateCullInstanceCountBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(0, lateCullDispatchIndirectArgsBuffer)
                };

                g_Graphic.AddComputePass(commandList, "gpuculling_CS_BuildLateCullIndirectArgs", bindingSetDesc, Vector3U{ 1, 1, 1 });
            }
        }
        else
        {
            if (bDoOcclusionCulling)
            {
                g_Graphic.AddComputePass(commandList, shaderName, bindingSetDesc, lateCullDispatchIndirectArgsBuffer, 0);
            }
        }
    }

    void RenderInstances(
        nvrhi::CommandListHandle commandList,
        const RenderGraph& renderGraph,
        const RenderBasePassParams& params,
        bool bAlphaMaskPrimitives)
    {
        PROFILE_FUNCTION();
        PROFILE_GPU_SCOPED(commandList, "Render Instances");

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& view = *params.m_View;

        const uint32_t nbInstances = bAlphaMaskPrimitives ? scene->m_AlphaMaskPrimitiveIDs.size() : scene->m_OpaquePrimitiveIDs.size();

        if (nbInstances == 0)
        {
            return;
        }

        nvrhi::BufferHandle instanceCountBuffer = renderGraph.GetBuffer(m_InstanceCountRDGBufferHandle);
        nvrhi::BufferHandle drawIndexedIndirectArgumentsBuffer = renderGraph.GetBuffer(m_DrawIndexedIndirectArgumentsRDGBufferHandle);
        nvrhi::BufferHandle startInstanceConstsOffsetsBuffer = renderGraph.GetBuffer(m_StartInstanceConstsOffsetsRDGBufferHandle);

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
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Graphic.m_GlobalVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Graphic.m_GlobalMaterialDataBuffer),
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
        PSODesc.PS = bAlphaMaskPrimitives ? params.m_PSAlphaMask : params.m_PS;
        PSODesc.renderState = params.m_RenderState;
        PSODesc.inputLayout = g_CommonResources.GPUCullingLayout;
        PSODesc.bindingLayouts = { bindingLayout, g_Graphic.m_BindlessLayout };

        nvrhi::GraphicsState drawState;
        drawState.framebuffer = frameBuffer;
        drawState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)viewportTexDesc.width, (float)viewportTexDesc.height });
        drawState.indexBuffer = { g_Graphic.m_GlobalIndexBuffer, g_Graphic.m_GlobalIndexBuffer->getDesc().format, 0 };
        drawState.vertexBuffers = { { startInstanceConstsOffsetsBuffer, 0, 0} };
        drawState.indirectParams = drawIndexedIndirectArgumentsBuffer;
        drawState.indirectCountBuffer = instanceCountBuffer;
        drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
        drawState.bindings = { bindingSet, g_Graphic.m_DescriptorTableManager->GetDescriptorTable() };

        commandList->setGraphicsState(drawState);

        // NOTE: treating the 2nd arg for 'drawIndexedIndirect' as 'MaxCommandCount' is only legit for d3d12!
        const uint32_t maxCommandCount = nbInstances;
        commandList->drawIndexedIndirect(0, std::max(1U, maxCommandCount));
    }

    void GenerateHZB(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        PROFILE_FUNCTION();
        PROFILE_GPU_SCOPED(commandList, "Generate HZB");

        Scene* scene = g_Graphic.m_Scene.get();

        const Vector2U HZBDims{ scene->m_HZB->getDesc().width, scene->m_HZB->getDesc().height };

        MinMaxDownsampleConsts PassParameters;
        PassParameters.m_OutputDimensions = HZBDims;
        PassParameters.m_bDownsampleMax = !Graphic::kInversedDepthBuffer;

        nvrhi::TextureHandle depthStencilBuffer = params.m_FrameBufferDesc.depthAttachment.texture;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(PassParameters)),
            nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0, scene->m_HZB),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
        };

        const Vector3U dispatchGroupSize = ComputeShaderUtils::GetGroupCount(HZBDims, 8);
        g_Graphic.AddComputePass(commandList, "minmaxdownsample_CS_Main", bindingSetDesc, dispatchGroupSize, &PassParameters, sizeof(PassParameters));

        // generate HZB mip chain
        const nvrhi::SamplerReductionType reductionType = Graphic::kInversedDepthBuffer ? nvrhi::SamplerReductionType::Minimum : nvrhi::SamplerReductionType::Maximum;
        m_SPDHelper.Execute(commandList, renderGraph, depthStencilBuffer, scene->m_HZB, reductionType);
    }

    void RenderBasePass(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        assert(params.m_View);

        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);
        commandList->clearBufferUInt(counterStatsBuffer, 0);

		GPUCulling(commandList, renderGraph, params, false /* bLateCull */, false /* bAlphaMaskPrimitives */);
		RenderInstances(commandList, renderGraph, params, false /* bAlphaMaskPrimitives */);

        const auto& controllables = g_GraphicPropertyGrid.m_InstanceRenderingControllables;
        const bool bDoOcclusionCulling = controllables.m_bEnableOcclusionCulling && m_bDoOcclusionCulling;
        if (bDoOcclusionCulling)
        {
            GenerateHZB(commandList, renderGraph, params);

            GPUCulling(commandList, renderGraph, params, true /* bLateCull */, false /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, false /* bAlphaMaskPrimitives */);

            GPUCulling(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, true /* bAlphaMaskPrimitives */);
            GPUCulling(commandList, renderGraph, params, true /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, true /* bAlphaMaskPrimitives */);

            GenerateHZB(commandList, renderGraph, params);
        }
        else
        {
			// cull & render for alpha mask primitives, but no occlusion culling
            GPUCulling(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, true /* bAlphaMaskPrimitives */);
        }

		// copy counter buffer, so that it can be read on CPU next frame
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        m_CounterStatsReadbackBuffer.CopyTo(device, commandList, counterStatsBuffer);
    }
};

class GBufferRenderer : public BasePassRenderer
{
public:
    GBufferRenderer() : BasePassRenderer("GBufferRenderer", true) {}

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

        if (scene->m_Primitives.empty())
        {
            return;
        }

        View& view = scene->m_Views[Scene::EView::Main];

        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(GBufferATexture);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer);

        // write 'opaque' to stencil buffer
        nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthWriteStencilWrite;
        depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
        depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;

        RenderBasePassParams params;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=0");
        params.m_PSAlphaMask = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=1");
        params.m_View = &view;
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, depthStencilState, g_CommonResources.CullNone };
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

class TransparentForwardRenderer : public BasePassRenderer
{
public:
    TransparentForwardRenderer() : BasePassRenderer("TransparentForwardRenderer", false) {}

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
    const uint32_t m_CSMIndex;

public:
    SunCSMBasePassRenderer(uint32_t CSMIdx)
        : BasePassRenderer(StringFormat("CSM: %d", CSMIdx), false)
        , m_CSMIndex(CSMIdx)
    {
        g_SunCSMBasePassRenderers[m_CSMIndex] = this;
    }

    void Initialize() override
	{
		BasePassRenderer::Initialize();
	}

    bool Setup(RenderGraph& renderGraph) override
    {
        const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;

        if (!shadowControllables.m_bEnabled)
        {
            return false;
        }

        BasePassRenderer::Setup(renderGraph);

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

        if (scene->m_Primitives.empty())
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
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, shadowDepthStencilState, Graphic::kFrontCCW ? g_CommonResources.CullCounterClockwise : g_CommonResources.CullClockwise };
        params.m_FrameBufferDesc = frameBufferDesc;

        RenderBasePass(commandList, renderGraph, params);
    }
};

static GBufferRenderer gs_GBufferRenderer;
IRenderer* g_GBufferRenderer = &gs_GBufferRenderer;

static TransparentForwardRenderer gs_TransparentForwardRenderer;
IRenderer* g_TransparentForwardRenderer = &gs_TransparentForwardRenderer;

static SunCSMBasePassRenderer gs_CSMRenderers[Graphic::kNbCSMCascades] = { 0, 1, 2, 3 };
