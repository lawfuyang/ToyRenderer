#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "CommonResources.h"
#include "Engine.h"
#include "FFXHelpers.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class UpdateInstanceConstsRenderer : public IRenderer
{
public:
    UpdateInstanceConstsRenderer() : IRenderer{ "UpdateInstanceConstsRenderer" } {}

    void CreateInstanceConstsBuffer(nvrhi::CommandListHandle commandList)
    {
        const uint32_t nbPrimitives = g_Scene->m_Primitives.size();
        if (nbPrimitives == 0)
        {
            return;
        }

        std::vector<BasePassInstanceConstants> instanceConstsBytes;
        instanceConstsBytes.reserve(nbPrimitives);

        for (const Primitive& primitive : g_Scene->m_Primitives)
        {
            assert(primitive.IsValid());

            const Node& node = g_Scene->m_Nodes.at(primitive.m_NodeID);
            const Material& material = primitive.m_Material;
            const Mesh& mesh = g_Graphic.m_Meshes.at(primitive.m_MeshIdx);

            // instance consts
            BasePassInstanceConstants instanceConsts{};
            instanceConsts.m_MeshDataIdx = mesh.m_MeshDataBufferIdx;
            instanceConsts.m_MaterialDataIdx = material.m_MaterialDataBufferIdx;

            // world matrices updated on GPU. see: CS_UpdateInstanceConsts

            instanceConstsBytes.push_back(instanceConsts);
        }

        nvrhi::BufferDesc desc;
        desc.byteSize = nbPrimitives * sizeof(BasePassInstanceConstants);
        desc.structStride = sizeof(BasePassInstanceConstants);
        desc.debugName = "Instance Consts Buffer";
        desc.canHaveUAVs = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        g_Scene->m_InstanceConstsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);

        commandList->writeBuffer(g_Scene->m_InstanceConstsBuffer, instanceConstsBytes.data(), instanceConstsBytes.size() * sizeof(BasePassInstanceConstants));
    }

    void CreateNodeTransformsBuffer(nvrhi::CommandListHandle commandList)
    {
        for (const Node& node : g_Scene->m_Nodes)
        {
            NodeLocalTransform& data = *(NodeLocalTransform*)&g_Scene->m_NodeLocalTransforms.emplace_back();
            data.m_ParentNodeIdx = node.m_ParentNodeID;
            data.m_Position = node.m_Position;
            data.m_Rotation = node.m_Rotation;
            data.m_Scale = node.m_Scale;
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = g_Scene->m_Nodes.size() * sizeof(NodeLocalTransform);
            desc.structStride = sizeof(NodeLocalTransform);
            desc.debugName = "Node Transforms Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;

            g_Scene->m_NodeLocalTransformsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        commandList->writeBuffer(g_Scene->m_NodeLocalTransformsBuffer, g_Scene->m_NodeLocalTransforms.data(), g_Scene->m_NodeLocalTransforms.size() * sizeof(NodeLocalTransform));

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = g_Scene->m_Primitives.size() * sizeof(uint32_t);
            desc.structStride = sizeof(uint32_t);
            desc.debugName = "PrimitiveIDToNodeID Buffer";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;

            g_Scene->m_PrimitiveIDToNodeIDBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
        }

        std::vector<uint32_t> primitiveIDToNodeIDBytes;
        for (const Primitive& primitive : g_Scene->m_Primitives)
        {
            primitiveIDToNodeIDBytes.push_back(primitive.m_NodeID);
        }

        commandList->writeBuffer(g_Scene->m_PrimitiveIDToNodeIDBuffer, primitiveIDToNodeIDBytes.data(), primitiveIDToNodeIDBytes.size() * sizeof(uint32_t));
    }

    void PostSceneLoad() override
    {
        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, __FUNCTION__);

        CreateInstanceConstsBuffer(commandList);
        CreateNodeTransformsBuffer(commandList);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (g_Scene->m_Primitives.empty())
        {
            return false;
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        {
            PROFILE_GPU_SCOPED(commandList, "Upload Node Transforms");
            commandList->writeBuffer(g_Scene->m_NodeLocalTransformsBuffer, g_Scene->m_NodeLocalTransforms.data(), g_Scene->m_NodeLocalTransforms.size() * sizeof(NodeLocalTransform));
        }

        const uint32_t numPrimitives = g_Scene->m_Primitives.size();

        UpdateInstanceConstsPassConstants passConstants;
        passConstants.m_NumInstances = numPrimitives;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, g_Scene->m_NodeLocalTransformsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Scene->m_PrimitiveIDToNodeIDBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(1, g_Scene->m_TLASInstanceDescsBuffer),
        };

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "updateinstanceconsts_CS_UpdateInstanceConstsAndBuildTLAS";
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(passConstants.m_NumInstances, kNumThreadsPerWave);
        computePassParams.m_PushConstantsData = &passConstants;
        computePassParams.m_PushConstantsBytes = sizeof(passConstants);

        g_Graphic.AddComputePass(computePassParams);

        // TODO: async compute this
        {
            PROFILE_GPU_SCOPED(commandList, "Build TLAS");
            commandList->buildTopLevelAccelStructFromBuffer(g_Scene->m_TLAS, g_Scene->m_TLASInstanceDescsBuffer, 0, numPrimitives);
        }
    }
};
static UpdateInstanceConstsRenderer gs_UpdateInstanceConstsRenderer;
IRenderer* g_UpdateInstanceConstsRenderer = &gs_UpdateInstanceConstsRenderer;

class BasePassRenderer : public IRenderer
{
    RenderGraph::ResourceHandle m_LateCullDispatchIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullInstanceCountBufferRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullInstanceIDsBufferRDGBufferHandle;

    FFXHelpers::SPD m_SPDHelper;

    RenderGraph::ResourceHandle m_MeshletAmplificationDataBufferRDGBufferHandle;
    RenderGraph::ResourceHandle m_MeshletDispatchArgumentsBufferRDGBufferHandle;

    nvrhi::PipelineStatisticsQueryHandle m_PipelineStatisticsQuery[2];
    nvrhi::PipelineStatistics m_LastPipelineStatistics;

    bool m_DoFrustumCulling = true;
    bool m_bDoOcclusionCulling = true;
    bool m_bDoMeshletConeCulling = true;
    uint32_t m_CullingFlags = 0;

    Vector2U m_HZBDimensions = Vector2U{ 1,1 };
    Vector4 m_CullingFrustum = Vector4{ 0.0f, 0.0f, 0.0f, 0.0f };

public:
    struct RenderBasePassParams
    {
        nvrhi::ShaderHandle m_PS;
        nvrhi::ShaderHandle m_PSAlphaMask;
        nvrhi::RenderState m_RenderState;
        nvrhi::FramebufferDesc m_FrameBufferDesc;
    };

    BasePassRenderer(const char* rendererName)
        : IRenderer(rendererName)
    {}

	void Initialize() override
	{
        for (uint32_t i = 0; i < 2; ++i)
        {
            m_PipelineStatisticsQuery[i] = g_Graphic.m_NVRHIDevice->createPipelineStatisticsQuery();
        }
	}

    void UpdateImgui() override
    {
        ImGui::Text("Primitives Invocations: %llu", m_LastPipelineStatistics.CInvocations);
        ImGui::Text("Primitives Primitives: %llu", m_LastPipelineStatistics.CPrimitives);
        ImGui::Text("PS Invocations: %llu", m_LastPipelineStatistics.PSInvocations);
        ImGui::Text("CS Invocations: %llu", m_LastPipelineStatistics.CSInvocations);
        ImGui::Text("AS Invocations: %llu", m_LastPipelineStatistics.ASInvocations);
        ImGui::Text("MS Invocations: %llu", m_LastPipelineStatistics.MSInvocations);
        ImGui::Text("MS Primitives: %llu", m_LastPipelineStatistics.MSPrimitives);
    }

	bool Setup(RenderGraph& renderGraph) override
	{
		const uint32_t nbInstances = g_Scene->m_Primitives.size();
        if (nbInstances == 0)
        {
            return true;
		}

        m_DoFrustumCulling = g_Scene->m_bEnableFrustumCulling;
        m_bDoOcclusionCulling = g_Scene->m_bEnableOcclusionCulling;
        m_bDoMeshletConeCulling = g_Scene->m_bEnableMeshletConeCulling;

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(MeshletAmplificationData) * GraphicConstants::kMaxThreadGroupsPerDimension;
            desc.structStride = sizeof(MeshletAmplificationData);
            desc.canHaveUAVs = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.debugName = "MeshletAmplificationDataBuffer";
            renderGraph.CreateTransientResource(m_MeshletAmplificationDataBufferRDGBufferHandle, desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(DispatchIndirectArguments);
            desc.structStride = sizeof(DispatchIndirectArguments);
            desc.canHaveUAVs = true;
            desc.isDrawIndirectArgs = true;
            desc.initialState = nvrhi::ResourceStates::IndirectArgument;
            desc.debugName = "MeshletDispatchArgumentsBuffer";
            renderGraph.CreateTransientResource(m_MeshletDispatchArgumentsBufferRDGBufferHandle, desc);
        }

		if (m_bDoOcclusionCulling)
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

				renderGraph.CreateTransientResource(m_LateCullInstanceIDsBufferRDGBufferHandle, desc);
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

        const uint32_t nbInstances = bAlphaMaskPrimitives ? g_Scene->m_AlphaMaskPrimitiveIDs.size() : g_Scene->m_OpaquePrimitiveIDs.size();
        if (nbInstances == 0)
        {
            return;
        }

        nvrhi::BufferHandle meshletAmplificationDataBuffer = renderGraph.GetBuffer(m_MeshletAmplificationDataBufferRDGBufferHandle);
        nvrhi::BufferHandle meshletDispatchArgumentsBuffer = renderGraph.GetBuffer(m_MeshletDispatchArgumentsBufferRDGBufferHandle);
        nvrhi::BufferHandle lateCullDispatchIndirectArgsBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullDispatchIndirectArgsRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
        nvrhi::BufferHandle lateCullInstanceCountBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullInstanceCountBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
		nvrhi::BufferHandle lateCullInstanceIDsBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullInstanceIDsBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;

        {
            PROFILE_GPU_SCOPED(commandList, "Clear Buffers");

            commandList->clearBufferUInt(meshletDispatchArgumentsBuffer, 0);

            if (!bLateCull && m_bDoOcclusionCulling)
            {
                commandList->clearBufferUInt(lateCullInstanceCountBuffer, 0);
				commandList->clearBufferUInt(lateCullInstanceIDsBuffer, 0);
            }
        }

        const uint32_t forcedMeshLOD = (g_Scene->m_ForceMeshLOD >= 0) ? g_Scene->m_ForceMeshLOD : kInvalidMeshLOD;

        GPUCullingPassConstants passParameters{};
        passParameters.m_NbInstances = nbInstances;
        passParameters.m_CullingFlags = m_CullingFlags;
        passParameters.m_Frustum = m_CullingFrustum;
        passParameters.m_HZBDimensions = m_HZBDimensions;
        passParameters.m_WorldToView = g_Scene->m_View.m_CullingWorldToView;
        passParameters.m_PrevWorldToView = g_Scene->m_View.m_CullingPrevWorldToView;
        passParameters.m_NearPlane = g_Scene->m_View.m_ZNearP;
        passParameters.m_P00 = g_Scene->m_View.m_ViewToClip.m[0][0];
        passParameters.m_P11 = g_Scene->m_View.m_ViewToClip.m[1][1];
        passParameters.m_ForcedMeshLOD =  forcedMeshLOD;
        passParameters.m_MeshLODTarget = (2.0f / g_Scene->m_View.m_ViewToClip.m[1][1]) * (1.0f / (float)g_Graphic.m_DisplayResolution.y);

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passParameters);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, bAlphaMaskPrimitives ? g_Scene->m_AlphaMaskInstanceIDsBuffer : g_Scene->m_OpaqueInstanceIDsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(3, m_bDoOcclusionCulling ? g_Scene->m_HZB : g_CommonResources.BlackTexture.m_NVRHITextureHandle),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, meshletAmplificationDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(1, meshletDispatchArgumentsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(2, lateCullInstanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(3, lateCullInstanceIDsBuffer),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampMinReductionSampler)
        };

        const std::string shaderName = StringFormat("gpuculling_CS_GPUCulling LATE_CULL=%d", bLateCull);

        if (!bLateCull)
        {
            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = shaderName;
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(nbInstances, kNumThreadsPerWave);

            g_Graphic.AddComputePass(computePassParams);

            if (m_bDoOcclusionCulling)
            {
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, lateCullInstanceCountBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(0, lateCullDispatchIndirectArgsBuffer)
                };

                computePassParams.m_ShaderName = "gpuculling_CS_BuildLateCullIndirectArgs";
                computePassParams.m_BindingSetDesc = bindingSetDesc;
                computePassParams.m_DispatchGroupSize = Vector3U{ 1, 1, 1 };

                g_Graphic.AddComputePass(computePassParams);
            }
        }
        else
        {
            if (m_bDoOcclusionCulling)
            {
                Graphic::ComputePassParams computePassParams;
                computePassParams.m_CommandList = commandList;
                computePassParams.m_ShaderName = shaderName;
                computePassParams.m_BindingSetDesc = bindingSetDesc;
                computePassParams.m_IndirectArgsBuffer = lateCullDispatchIndirectArgsBuffer;

                g_Graphic.AddComputePass(computePassParams);
            }
        }
    }

    void RenderInstances(
        nvrhi::CommandListHandle commandList,
        const RenderGraph& renderGraph,
        const RenderBasePassParams& params,
        bool bIsLateCull,
        bool bAlphaMaskPrimitives)
    {
        PROFILE_FUNCTION();
        PROFILE_GPU_SCOPED(commandList, "Render Instances");

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

		std::span<const uint32_t> primitivesIDs = bAlphaMaskPrimitives ? g_Scene->m_AlphaMaskPrimitiveIDs : g_Scene->m_OpaquePrimitiveIDs;
        const uint32_t nbInstances = primitivesIDs.size();

        if (nbInstances == 0)
        {
            return;
        }

        nvrhi::BufferHandle meshletAmplificationDataBuffer = renderGraph.GetBuffer(m_MeshletAmplificationDataBufferRDGBufferHandle);
        nvrhi::BufferHandle meshletDispatchArgumentsBuffer = renderGraph.GetBuffer(m_MeshletDispatchArgumentsBufferRDGBufferHandle);

        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(params.m_FrameBufferDesc);
        const nvrhi::FramebufferAttachment& depthAttachment = params.m_FrameBufferDesc.depthAttachment;
        const nvrhi::TextureDesc& viewportTexDesc = depthAttachment.texture ? depthAttachment.texture->getDesc() : params.m_FrameBufferDesc.colorAttachments[0].texture->getDesc();

        nvrhi::RenderState finalRenderState = params.m_RenderState;
        uint32_t finalCullingFlags = m_CullingFlags;

        // assume alpha mask primitives are double-sided too, & ignore cone culling
        if (bAlphaMaskPrimitives)
        {
            finalRenderState.rasterState = g_CommonResources.CullNone;
            finalRenderState.depthStencilState.backFaceStencil.passOp = nvrhi::StencilOp::Replace;
            finalCullingFlags = finalCullingFlags & ~kCullingFlagMeshletConeCullingEnable;
        }

        // pass consts
        BasePassConstants basePassConstants;
        basePassConstants.m_WorldToClip = g_Scene->m_View.m_WorldToClip;
        basePassConstants.m_PrevWorldToClip = g_Scene->m_View.m_PrevWorldToClip;
        basePassConstants.m_WorldToView = g_Scene->m_View.m_CullingWorldToView;
        basePassConstants.m_Frustum = m_CullingFrustum;
        basePassConstants.m_CullingFlags = finalCullingFlags;
        basePassConstants.m_HZBDimensions = m_HZBDimensions;
        basePassConstants.m_P00 = g_Scene->m_View.m_ViewToClip.m[0][0];
        basePassConstants.m_P11 = g_Scene->m_View.m_ViewToClip.m[1][1];
        basePassConstants.m_NearPlane = g_Scene->m_View.m_ZNearP;
        basePassConstants.m_DebugMode = g_Scene->m_DebugViewMode;
        basePassConstants.m_OutputResolution = Vector2U{ viewportTexDesc.width, viewportTexDesc.height };

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, basePassConstants);

        // bind and set root signature
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, g_Graphic.m_GlobalVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Graphic.m_GlobalMaterialDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, g_Graphic.m_GlobalMeshletDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Graphic.m_GlobalMeshletVertexOffsetsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalMeshletIndicesBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, meshletAmplificationDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(8, m_bDoOcclusionCulling ? g_Scene->m_HZB : g_CommonResources.BlackTexture.m_NVRHITextureHandle),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler),
            nvrhi::BindingSetItem::Sampler(4, g_CommonResources.LinearClampMinReductionSampler)
        };

        nvrhi::BindingSetHandle bindingSet;
        nvrhi::BindingLayoutHandle bindingLayout;
        g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

        nvrhi::MeshletPipelineDesc PSODesc;
        PSODesc.AS = g_Graphic.GetShader(StringFormat("basepass_AS_Main LATE_CULL=%d", bIsLateCull));
        PSODesc.MS = g_Graphic.GetShader("basepass_MS_Main");
        PSODesc.PS = bAlphaMaskPrimitives ? params.m_PSAlphaMask : params.m_PS;
        PSODesc.renderState = finalRenderState;
        PSODesc.bindingLayouts = { bindingLayout, g_Graphic.m_SrvUavCbvBindlessLayout };

        nvrhi::MeshletState meshletState;
        meshletState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
        meshletState.framebuffer = frameBuffer;
        meshletState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)viewportTexDesc.width, (float)viewportTexDesc.height });
        meshletState.indirectParams = meshletDispatchArgumentsBuffer;
        meshletState.bindings = { bindingSet, g_Graphic.GetSrvUavCbvDescriptorTable() };

        commandList->setMeshletState(meshletState);

        commandList->dispatchMeshIndirect(0);
    }

    void GenerateHZB(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        if (g_Scene->m_bFreezeCullingCamera)
        {
            return;
        }

        PROFILE_FUNCTION();
        PROFILE_GPU_SCOPED(commandList, "Generate HZB");

        MinMaxDownsampleConsts passParameters;
        passParameters.m_OutputDimensions = m_HZBDimensions;
        passParameters.m_bDownsampleMax = !GraphicConstants::kInversedDepthBuffer;

        nvrhi::TextureHandle depthStencilBuffer = params.m_FrameBufferDesc.depthAttachment.texture;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
            nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0, g_Scene->m_HZB),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
        };

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "minmaxdownsample_CS_Main";
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(m_HZBDimensions, 8);
        computePassParams.m_PushConstantsData = &passParameters;
        computePassParams.m_PushConstantsBytes = sizeof(passParameters);

        g_Graphic.AddComputePass(computePassParams);

        // generate HZB mip chain
        const nvrhi::SamplerReductionType reductionType = GraphicConstants::kInversedDepthBuffer ? nvrhi::SamplerReductionType::Minimum : nvrhi::SamplerReductionType::Maximum;
        m_SPDHelper.Execute(commandList, renderGraph, depthStencilBuffer, g_Scene->m_HZB, reductionType);
    }

    void RenderBasePass(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        m_LastPipelineStatistics = device->getPipelineStatistics(m_PipelineStatisticsQuery[g_Graphic.m_FrameCounter % 2]);
        AUTO_SCOPE([&]{ commandList->beginPipelineStatisticsQuery(m_PipelineStatisticsQuery[g_Graphic.m_FrameCounter % 2]); }, [&]{ commandList->endPipelineStatisticsQuery(m_PipelineStatisticsQuery[g_Graphic.m_FrameCounter % 2]); });

        m_CullingFlags = m_DoFrustumCulling ? kCullingFlagFrustumCullingEnable : 0;
        m_CullingFlags |= m_bDoOcclusionCulling ? kCullingFlagOcclusionCullingEnable : 0;
        m_CullingFlags |= m_bDoMeshletConeCulling ? kCullingFlagMeshletConeCullingEnable : 0;

        m_HZBDimensions = m_bDoOcclusionCulling ? Vector2U{ g_Scene->m_HZB->getDesc().width, g_Scene->m_HZB->getDesc().height } : Vector2U{ 1, 1 };

        Matrix projectionT = g_Scene->m_View.m_ViewToClip.Transpose();
        Vector4 frustumX = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[0] };
        Vector4 frustumY = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[1] };
        frustumX.Normalize();
        frustumY.Normalize();

        m_CullingFrustum = Vector4{ frustumX.x, frustumX.z, frustumY.y, frustumY.z };

        GPUCulling(commandList, renderGraph, params, false /* bLateCull */, false /* bAlphaMaskPrimitives */);
        RenderInstances(commandList, renderGraph, params, false /* bLateCull */, false /* bAlphaMaskPrimitives */);

        if (m_bDoOcclusionCulling)
        {
            GenerateHZB(commandList, renderGraph, params);

            GPUCulling(commandList, renderGraph, params, true /* bLateCull */, false /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, true /* bLateCull */, false /* bAlphaMaskPrimitives */);

            GPUCulling(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
            GPUCulling(commandList, renderGraph, params, true /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, true /* bLateCull */, true /* bAlphaMaskPrimitives */);

            GenerateHZB(commandList, renderGraph, params);
        }
        else
        {
            // cull & render for alpha mask primitives, but no occlusion culling
            GPUCulling(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
            RenderInstances(commandList, renderGraph, params, false /* bLateCull */, true /* bAlphaMaskPrimitives */);
        }
    }
};

class GBufferRenderer : public BasePassRenderer
{
public:
    GBufferRenderer() : BasePassRenderer("GBufferRenderer") {}

	void Initialize() override
	{
		BasePassRenderer::Initialize();

		nvrhi::TextureDesc desc;
		desc.width = GetNextPow2(g_Graphic.m_RenderResolution.x) >> 1;
		desc.height = GetNextPow2(g_Graphic.m_RenderResolution.y) >> 1;
		desc.format = GraphicConstants::kHZBFormat;
		desc.isUAV = true;
		desc.debugName = "HZB";
		desc.mipLevels = ComputeNbMips(desc.width, desc.height);
		desc.useClearValue = false;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;

		g_Scene->m_HZB = g_Graphic.m_NVRHIDevice->createTexture(desc);

		nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
		SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "GBufferRenderer::Initialize");

		commandList->clearTextureFloat(g_Scene->m_HZB, nvrhi::AllSubresources, nvrhi::Color{ GraphicConstants::kFarDepth });
	}

    bool Setup(RenderGraph& renderGraph) override
    {
		BasePassRenderer::Setup(renderGraph);

        {
            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.isRenderTarget = true;
            desc.setClearValue(nvrhi::Color{ 0.0f });
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.format = GraphicConstants::kGBufferAFormat;
            desc.debugName = "GBufferA";
            renderGraph.CreateTransientResource(g_GBufferARDGTextureHandle, desc);
        }

        {
            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.isRenderTarget = true;
            desc.setClearValue(nvrhi::Color{ 0.0f });
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.format = GraphicConstants::kGBufferMotionFormat;
            desc.debugName = "GBufferMotion";
            renderGraph.CreateTransientResource(g_GBufferMotionRDGTextureHandle, desc);
        }

        {
            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.format = GraphicConstants::kDepthStencilFormat;
            desc.debugName = "Depth Buffer";
            desc.isRenderTarget = true;
            desc.setClearValue(nvrhi::Color{ GraphicConstants::kFarDepth, GraphicConstants::kStencilBit_Sky, 0.0f, 0.0f });
            desc.initialState = nvrhi::ResourceStates::DepthRead;
            renderGraph.CreateTransientResource(g_DepthStencilBufferRDGTextureHandle, desc);

            desc.format = GraphicConstants::kDepthBufferCopyFormat;
            desc.debugName = "Depth Buffer Copy";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(g_DepthBufferCopyRDGTextureHandle, desc);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        if (g_Scene->m_Primitives.empty())
        {
            return;
        }
        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle GBufferMotionTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
        nvrhi::TextureHandle depthStencilBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(GBufferATexture);
        frameBufferDesc.addColorAttachment(GBufferMotionTexture);
        frameBufferDesc.setDepthAttachment(depthStencilBuffer);

        nvrhi::BlendState blendState;
        blendState.targets[0] = g_CommonResources.BlendOpaque;

        // write 'opaque' to stencil buffer
        nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthWriteStencilWrite;
        depthStencilState.stencilRefValue = GraphicConstants::kStencilBit_Opaque;
        depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;

        RenderBasePassParams params;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=0");
        params.m_PSAlphaMask = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=1");
        params.m_RenderState = nvrhi::RenderState{ blendState, depthStencilState, g_CommonResources.CullBackFace };
        params.m_FrameBufferDesc = frameBufferDesc;

        RenderBasePass(commandList, renderGraph, params);

        // at this point, we have the final depth buffer. create a copy for SRV purposes
        {
            PROFILE_GPU_SCOPED(commandList, "Copy depth buffer");

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::Texture_SRV(0, depthStencilBuffer),
            };

            nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

            nvrhi::FramebufferDesc frameBufferDescDepthBufferCopy;
            frameBufferDescDepthBufferCopy.addColorAttachment(depthBufferCopy);

            Graphic::FullScreenPassParams fullScreenPassParams;
            fullScreenPassParams.m_CommandList = commandList;
            fullScreenPassParams.m_FrameBufferDesc = frameBufferDescDepthBufferCopy;
            fullScreenPassParams.m_BindingSetDesc = bindingSetDesc;
            fullScreenPassParams.m_ShaderName = "fullscreen_PS_Passthrough";

            g_Graphic.AddFullScreenPass(fullScreenPassParams);
        }
    }
};

class TransparentForwardRenderer : public BasePassRenderer
{
public:
    TransparentForwardRenderer() : BasePassRenderer("TransparentForwardRenderer") {}

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

static GBufferRenderer gs_GBufferRenderer;
IRenderer* g_GBufferRenderer = &gs_GBufferRenderer;

static TransparentForwardRenderer gs_TransparentForwardRenderer;
IRenderer* g_TransparentForwardRenderer = &gs_TransparentForwardRenderer;
