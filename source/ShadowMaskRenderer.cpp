#include "Graphic.h"

#include "CommonResources.h"
#include "FFXHelpers.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/shared/ShadowMaskStructs.h"
#include "shaders/shared/MinMaxDownsampleStructs.h"

RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class ShadowMaskRenderer : public IRenderer
{
public:
	ShadowMaskRenderer() : IRenderer("ShadowMaskRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
    {
		const auto& shadowControllables = g_GraphicPropertyGrid.m_ShadowControllables;
		if (!shadowControllables.m_bEnabled)
		{
			return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.format = nvrhi::Format::R8_UNORM;
		desc.debugName = "Shadow Mask Texture";
		desc.isRenderTarget = true;
		desc.isUAV = true;
		desc.setClearValue(nvrhi::Color{ 1.0f });
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		renderGraph.CreateTransientResource(g_ShadowMaskRDGTextureHandle, desc);

		renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

        return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		Scene* scene = g_Graphic.m_Scene.get();
		View& view = scene->m_View;

		nvrhi::TextureHandle shadowMaskTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

		HardwareRaytraceConsts passConstants;
		passConstants.m_InvViewProjMatrix = view.m_InvViewProjectionMatrix;
		passConstants.m_DirectionalLightDirection = scene->m_DirLightVec;
		passConstants.m_OutputResolution = Vector2U{ shadowMaskTexture->getDesc().width , shadowMaskTexture->getDesc().height };
		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopy),
			nvrhi::BindingSetItem::RayTracingAccelStruct(1, scene->m_TLAS),
			nvrhi::BindingSetItem::Texture_UAV(0, shadowMaskTexture)
		};

		nvrhi::BindingSetHandle bindingSet;
		nvrhi::BindingLayoutHandle bindingLayout;
		g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

		nvrhi::rt::PipelineDesc pipelineDesc;
		pipelineDesc.shaders = {
			{ "", g_Graphic.GetShader("shadowmask_RT_RayGen"), nullptr },
			{ "", g_Graphic.GetShader("shadowmask_RT_Miss"), nullptr },
		};
		pipelineDesc.globalBindingLayouts = { bindingLayout, g_Graphic.m_BindlessLayout };
		pipelineDesc.maxPayloadSize = sizeof(Vector4);

		nvrhi::rt::PipelineHandle pipeline = g_Graphic.GetOrCreatePSO(pipelineDesc);
		nvrhi::rt::ShaderTableHandle shaderTable = pipeline->createShaderTable();
		shaderTable->setRayGenerationShader("RT_RayGen");
		shaderTable->addMissShader("RT_Miss");

		nvrhi::rt::State state;
		state.shaderTable = shaderTable;
		state.bindings = { bindingSet };
		commandList->setRayTracingState(state);

		nvrhi::rt::DispatchRaysArguments args;
		args.width = passConstants.m_OutputResolution.x;
		args.height = passConstants.m_OutputResolution.y;
		commandList->dispatchRays(args);
	}
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
