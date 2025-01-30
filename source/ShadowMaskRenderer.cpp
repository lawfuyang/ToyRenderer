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
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;

class ShadowMaskRenderer : public IRenderer
{
public:
	ShadowMaskRenderer() : IRenderer("ShadowMaskRenderer") {}

	bool Setup(RenderGraph& renderGraph) override
    {
		if (!g_Scene->IsShadowsEnabled())
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
        renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);

        return true;
    }

	void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
	{
		nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
		View& view = g_Scene->m_View;

		nvrhi::TextureHandle shadowMaskTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
		nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

		ShadowMaskConsts passConstants;
		passConstants.m_ClipToWorld = view.m_ClipToWorld;
		passConstants.m_DirectionalLightDirection = g_Scene->m_DirLightVec;
		passConstants.m_OutputResolution = Vector2U{ shadowMaskTexture->getDesc().width , shadowMaskTexture->getDesc().height };
		nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
			nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopy),
			nvrhi::BindingSetItem::RayTracingAccelStruct(1, g_Scene->m_TLAS),
            nvrhi::BindingSetItem::Texture_SRV(2, GBufferATexture),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Scene->m_InstanceConstsBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, g_Graphic.m_GlobalVertexBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Graphic.m_GlobalMeshDataBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalMaterialDataBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(7, g_Graphic.m_GlobalIndexBuffer),
			nvrhi::BindingSetItem::Texture_UAV(0, shadowMaskTexture),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
			nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler),
		};

        g_Graphic.AddComputePass(commandList, "shadowmask_CS_ShadowMask", bindingSetDesc, ComputeShaderUtils::GetGroupCount(passConstants.m_OutputResolution, 8));
	}
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
