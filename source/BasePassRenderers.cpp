#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "CommonResources.h"
#include "DescriptorTableManager.h"
#include "Engine.h"
#include "FFXHelpers.h"
#include "RenderGraph.h"
#include "Scene.h"

#include "shaders/ShaderInterop.h"

static_assert(sizeof(DrawIndirectArguments) == sizeof(nvrhi::DrawIndirectArguments));
static_assert(sizeof(DrawIndexedIndirectArguments) == sizeof(nvrhi::DrawIndexedIndirectArguments));

static_assert(SamplerIdx_AnisotropicClamp == (int)nvrhi::SamplerAddressMode::Clamp);
static_assert(SamplerIdx_AnisotropicWrap == (int)nvrhi::SamplerAddressMode::Wrap);
static_assert(SamplerIdx_AnisotropicBorder == (int)nvrhi::SamplerAddressMode::Border);
static_assert(SamplerIdx_AnisotropicMirror == (int)nvrhi::SamplerAddressMode::Mirror);

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

        nvrhi::BufferHandle passConstantsBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

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

class PipelineStatisticsQuery
{
public:
    struct ScopedQuery
    {
        ScopedQuery(PipelineStatisticsQuery& query, nvrhi::CommandListHandle commandList)
            : m_Query(query)
            , m_CommandList(commandList)
        {
            g_Graphic.m_NVRHIDevice->resetPipelineStatisticsQuery(m_Query.m_PipelineStatisticsQueryHandle[m_Query.m_Counter]);
            commandList->beginPipelineStatisticsQuery(m_Query.m_PipelineStatisticsQueryHandle[m_Query.m_Counter]);
        }

        ~ScopedQuery()
        {
            m_CommandList->endPipelineStatisticsQuery(m_Query.m_PipelineStatisticsQueryHandle[m_Query.m_Counter]);
            m_Query.m_Counter = (m_Query.m_Counter + 1) % kQueuedFramesCount;
        }

        PipelineStatisticsQuery& m_Query;
        nvrhi::CommandListHandle m_CommandList;
    };

    void Initialize()
    {
        for (uint32_t i = 0; i < kQueuedFramesCount; ++i)
        {
            m_PipelineStatisticsQueryHandle[i] = g_Graphic.m_NVRHIDevice->createPipelineStatisticsQuery();
        }
    }

    nvrhi::PipelineStatistics GetLastValid() const
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        for (int i = m_Counter - 1; i >= 0; i--)
        {
            if (device->pollPipelineStatisticsQuery(m_PipelineStatisticsQueryHandle[i]))
                return device->getPipelineStatistics(m_PipelineStatisticsQueryHandle[i]);
        }

        for (int i = kQueuedFramesCount - 1; i > m_Counter; i--)
        {
            if (device->pollPipelineStatisticsQuery(m_PipelineStatisticsQueryHandle[i]))
                return device->getPipelineStatistics(m_PipelineStatisticsQueryHandle[i]);
        }

        return {};
    }

private:
    static const uint32_t kQueuedFramesCount = 5;
    nvrhi::PipelineStatisticsQueryHandle m_PipelineStatisticsQueryHandle[kQueuedFramesCount];

    uint32_t m_Counter = 0;
};
#define SCOPED_PIPELINE_STATISTICS_QUERY(query, commandList) PipelineStatisticsQuery::ScopedQuery scopedQuery(query, commandList)

class BasePassRenderer : public IRenderer
{
    RenderGraph::ResourceHandle m_InstanceCountRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullDispatchIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullInstanceCountBufferRDGBufferHandle;
    RenderGraph::ResourceHandle m_LateCullInstanceIDsBufferRDGBufferHandle;

    FFXHelpers::SPD m_SPDHelper;
    FencedReadbackBuffer m_CounterStatsReadbackBuffer;
    RenderGraph::ResourceHandle m_CounterStatsRDGBufferHandle;

    RenderGraph::ResourceHandle m_MeshletAmplificationDataBufferRDGBufferHandle;
    RenderGraph::ResourceHandle m_MeshletDispatchArgumentsBufferRDGBufferHandle;

    PipelineStatisticsQuery m_PipelineStatisticsQuery;

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
		m_CounterStatsReadbackBuffer.Initialize(sizeof(uint32_t) * kNbGPUCullingBufferCounters);

        m_PipelineStatisticsQuery.Initialize();
	}

    void UpdateImgui() override
    {
        const nvrhi::PipelineStatistics stats = m_PipelineStatisticsQuery.GetLastValid();

        ImGui::Text("Primitives Invocations: %llu", stats.CInvocations);
        ImGui::Text("Primitives Renderered: %llu", stats.CPrimitives);
        ImGui::Text("PS Invocations: %llu", stats.PSInvocations);
        ImGui::Text("CS Invocations: %llu", stats.CSInvocations);
        ImGui::Text("AS Invocations: %llu", stats.ASInvocations);
        ImGui::Text("MS Invocations: %llu", stats.MSInvocations);
        ImGui::Text("MS Primitives: %llu", stats.MSPrimitives);
    }

	bool Setup(RenderGraph& renderGraph) override
	{
		const uint32_t nbInstances = g_Graphic.m_Scene->m_Primitives.size();
        if (nbInstances == 0)
        {
            return true;
		}

        m_DoFrustumCulling = g_Scene->m_bEnableFrustumCulling;
        m_bDoOcclusionCulling = g_Scene->m_bEnableOcclusionCulling;
        m_bDoMeshletConeCulling = g_Scene->m_bEnableMeshletConeCulling;

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
            desc.byteSize = sizeof(MeshletAmplificationData) * Graphic::kMaxThreadGroupsPerDimension;
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

        nvrhi::BufferHandle instanceCountBuffer = renderGraph.GetBuffer(m_InstanceCountRDGBufferHandle);
        nvrhi::BufferHandle meshletAmplificationDataBuffer = renderGraph.GetBuffer(m_MeshletAmplificationDataBufferRDGBufferHandle);
        nvrhi::BufferHandle meshletDispatchArgumentsBuffer = renderGraph.GetBuffer(m_MeshletDispatchArgumentsBufferRDGBufferHandle);
        nvrhi::BufferHandle lateCullDispatchIndirectArgsBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullDispatchIndirectArgsRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
        nvrhi::BufferHandle lateCullInstanceCountBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullInstanceCountBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
		nvrhi::BufferHandle lateCullInstanceIDsBuffer = m_bDoOcclusionCulling ? renderGraph.GetBuffer(m_LateCullInstanceIDsBufferRDGBufferHandle) : g_CommonResources.DummyUIntStructuredBuffer;
        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);

        {
            PROFILE_GPU_SCOPED(commandList, "Clear Buffers");

			commandList->clearBufferUInt(instanceCountBuffer, 0);
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
            nvrhi::BindingSetItem::StructuredBuffer_UAV(2, instanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(3, counterStatsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(4, lateCullInstanceCountBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(5, lateCullInstanceIDsBuffer),
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

        nvrhi::BufferHandle instanceCountBuffer = renderGraph.GetBuffer(m_InstanceCountRDGBufferHandle);
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
        PSODesc.bindingLayouts = { bindingLayout, g_Graphic.m_BindlessLayout };

        nvrhi::MeshletState meshletState;
        meshletState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
        meshletState.framebuffer = frameBuffer;
        meshletState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)viewportTexDesc.width, (float)viewportTexDesc.height });
        meshletState.indirectParams = meshletDispatchArgumentsBuffer;
        meshletState.bindings = { bindingSet, g_Graphic.m_DescriptorTableManager->GetDescriptorTable() };

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
        passParameters.m_bDownsampleMax = !Graphic::kInversedDepthBuffer;

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
        const nvrhi::SamplerReductionType reductionType = Graphic::kInversedDepthBuffer ? nvrhi::SamplerReductionType::Minimum : nvrhi::SamplerReductionType::Maximum;
        m_SPDHelper.Execute(commandList, renderGraph, depthStencilBuffer, g_Scene->m_HZB, reductionType);
    }

    void RenderBasePass(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph, const RenderBasePassParams& params)
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        SCOPED_PIPELINE_STATISTICS_QUERY(m_PipelineStatisticsQuery, commandList);

        nvrhi::BufferHandle counterStatsBuffer = renderGraph.GetBuffer(m_CounterStatsRDGBufferHandle);
        commandList->clearBufferUInt(counterStatsBuffer, 0);

        // read back nb visible instances from counter
        {
            uint32_t readbackResults[kNbGPUCullingBufferCounters]{};
            m_CounterStatsReadbackBuffer.Read(readbackResults);

            // TODO: support transparent
            GPUCullingCounters& cullingCounters = g_Scene->m_View.m_GPUCullingCounters;
            cullingCounters.m_EarlyInstances = readbackResults[kCullingEarlyInstancesBufferCounterIdx];
            cullingCounters.m_LateInstances = readbackResults[kCullingLateInstancesBufferCounterIdx];
        }

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

		// copy counter buffer, so that it can be read on CPU next frame
        m_CounterStatsReadbackBuffer.CopyTo(commandList, counterStatsBuffer);
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
		desc.format = Graphic::kHZBFormat;
		desc.isUAV = true;
		desc.debugName = "HZB";
		desc.mipLevels = ComputeNbMips(desc.width, desc.height);
		desc.useClearValue = false;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;

		g_Scene->m_HZB = g_Graphic.m_NVRHIDevice->createTexture(desc);

		nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
		SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "GBufferRenderer::Initialize");

		commandList->clearTextureFloat(g_Scene->m_HZB, nvrhi::AllSubresources, nvrhi::Color{ Graphic::kFarDepth });
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
            desc.format = Graphic::kGBufferAFormat;
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
            desc.format = Graphic::kGBufferMotionFormat;
            desc.debugName = "GBufferMotion";
            renderGraph.CreateTransientResource(g_GBufferMotionRDGTextureHandle, desc);
        }

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

        // write 'opaque' to stencil buffer
        nvrhi::DepthStencilState depthStencilState = g_CommonResources.DepthWriteStencilWrite;
        depthStencilState.stencilRefValue = Graphic::kStencilBit_Opaque;
        depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;

        RenderBasePassParams params;
        params.m_PS = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=0");
        params.m_PSAlphaMask = g_Graphic.GetShader("basepass_PS_Main_GBuffer ALPHA_MASK_MODE=1");
        params.m_RenderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, depthStencilState, g_CommonResources.CullBackFace };
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
