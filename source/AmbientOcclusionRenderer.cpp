#include "Graphic.h"

#include "extern/imgui/imgui.h"
#include "extern/xegtao/XeGTAO.h"

#include "CommonResources.h"
#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/ShaderInterop.h"

RenderGraph::ResourceHandle g_SSAORDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class AmbientOcclusionRenderer : public IRenderer
{
    static const nvrhi::Format kWorkingDepthBufferFormat = nvrhi::Format::R16_FLOAT;

    int m_DebugOutputMode = 0;
    XeGTAO::GTAOSettings m_XeGTAOSettings;

    RenderGraph::ResourceHandle m_WorkingDepthBufferRDGTextureHandle;
    RenderGraph::ResourceHandle m_WorkingSSAORDGTextureHandle;
    RenderGraph::ResourceHandle m_WorkingEdgesRDGTextureHandle;
    RenderGraph::ResourceHandle m_DebugOutputRDGTextureHandle;

    nvrhi::TextureHandle m_HilbertLUT;

public:
    AmbientOcclusionRenderer() : IRenderer("AmbientOcclusionRenderer") {}

    void Initialize() override
    {
        m_XeGTAOSettings.QualityLevel = 3;
        m_XeGTAOSettings.DenoisePasses = 3;

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "AmbientOcclusionRenderer Init");

		const uint32_t kTexDim = 64;

		nvrhi::TextureDesc desc;
		desc.width = kTexDim;
		desc.height = kTexDim;
		desc.format = nvrhi::Format::R16_UINT;
		desc.debugName = "Hilbert LUT";
		desc.initialState = nvrhi::ResourceStates::ShaderResource;

		std::vector<uint16_t> data;
		data.resize(kTexDim * kTexDim);

		for (int x = 0; x < kTexDim; x++)
		{
			for (int y = 0; y < kTexDim; y++)
			{
				uint32_t r2index = XeGTAO::HilbertIndex(x, y);
				assert(r2index < 65536);
				data[x + 64 * y] = (uint16_t)r2index;
			}
		}

		m_HilbertLUT = device->createTexture(desc);

		commandList->writeTexture(m_HilbertLUT, 0, 0, data.data(), kTexDim * nvrhi::getFormatInfo(desc.format).bytesPerBlock);
		commandList->setPermanentTextureState(m_HilbertLUT, nvrhi::ResourceStates::ShaderResource);
		commandList->commitBarriers();

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(float);
            desc.structStride = sizeof(float);
            desc.debugName = "Exposure Buffer";
            desc.canHaveTypedViews = true;
            desc.canHaveUAVs = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;

            scene->m_LuminanceBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);

            const float kInitialExposure = 1.0f;
            commandList->writeBuffer(scene->m_LuminanceBuffer, &kInitialExposure, sizeof(float));
        }
	}

	void UpdateImgui() override
	{
		ImGui::Combo("Debug Output Mode", &m_DebugOutputMode, "None\0Screen-Space Normals\0Edges\0Bent Normals\0");
		ImGui::Separator();

		XeGTAO::GTAOImGuiSettings(m_XeGTAOSettings);
	}

    bool Setup(RenderGraph& renderGraph) override
	{
        const GraphicPropertyGrid::AmbientOcclusionControllables& AOControllables = g_GraphicPropertyGrid.m_AmbientOcclusionControllables;

        if (!AOControllables.m_bEnabled)
        {
            return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = g_Graphic.m_RenderResolution.x;
		desc.height = g_Graphic.m_RenderResolution.y;
		desc.mipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
		desc.format = kWorkingDepthBufferFormat;
		desc.debugName = "XeGTAO Working Depth Buffer";
		desc.isUAV = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		renderGraph.CreateTransientResource(m_WorkingDepthBufferRDGTextureHandle, desc);

		desc.mipLevels = 1;
		desc.format = Graphic::kSSAOOutputFormat;
		desc.debugName = "SSAO Buffer";
		renderGraph.CreateTransientResource(g_SSAORDGTextureHandle, desc);

		desc.format = Graphic::kSSAOOutputFormat;
		desc.debugName = "Working SSAO Texture";
		renderGraph.CreateTransientResource(m_WorkingSSAORDGTextureHandle, desc);

		desc.format = nvrhi::Format::R8_UNORM;
		desc.debugName = "Working Edges Texture";
		renderGraph.CreateTransientResource(m_WorkingEdgesRDGTextureHandle,desc);

        if (m_DebugOutputMode != 0)
        {
            desc.format = nvrhi::Format::RGBA16_SNORM;
            desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            desc.debugName = "Debug Output Texture";
            renderGraph.CreateTransientResource(m_DebugOutputRDGTextureHandle, desc);
        }

		renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

		return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;
        Scene* scene = g_Graphic.m_Scene.get();
        View& mainView = scene->m_View;

        XeGTAO::GTAOConstants GTAOconsts{};
        const bool bRowMajor = true;
        const uint32_t frameCounter = g_Graphic.m_FrameCounter % 256;
        XeGTAO::GTAOUpdateConstants(GTAOconsts, g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y, m_XeGTAOSettings, (const float*)&mainView.m_ViewToClip.m, bRowMajor, frameCounter);

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, GTAOconsts);

        nvrhi::TextureHandle workingDepthBuffer = renderGraph.GetTexture(m_WorkingDepthBufferRDGTextureHandle);
        nvrhi::TextureHandle workingSSAOTexture = renderGraph.GetTexture(m_WorkingSSAORDGTextureHandle);
        nvrhi::TextureHandle workingEdgesTexture = renderGraph.GetTexture(m_WorkingEdgesRDGTextureHandle);
        nvrhi::TextureHandle depthBufferCopyTexture = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);

        // generate depth mips
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopyTexture),
                nvrhi::BindingSetItem::Texture_UAV(0, workingDepthBuffer, kWorkingDepthBufferFormat, nvrhi::TextureSubresourceSet{ 0, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
                nvrhi::BindingSetItem::Texture_UAV(1, workingDepthBuffer, kWorkingDepthBufferFormat, nvrhi::TextureSubresourceSet{ 1, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
                nvrhi::BindingSetItem::Texture_UAV(2, workingDepthBuffer, kWorkingDepthBufferFormat, nvrhi::TextureSubresourceSet{ 2, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
                nvrhi::BindingSetItem::Texture_UAV(3, workingDepthBuffer, kWorkingDepthBufferFormat, nvrhi::TextureSubresourceSet{ 3, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
                nvrhi::BindingSetItem::Texture_UAV(4, workingDepthBuffer, kWorkingDepthBufferFormat, nvrhi::TextureSubresourceSet{ 4, 1, 0, nvrhi::TextureSubresourceSet::AllArraySlices }),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "ambientocclusion_CS_XeGTAO_PrefilterDepths";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector2U{ workingDepthBuffer->getDesc().width, workingDepthBuffer->getDesc().height }, Vector2U{ 16, 16 });

            g_Graphic.AddComputePass(computePassParams);
        }

        // main pass
        {
            XeGTAOMainPassConstantBuffer mainPassConsts{};
            mainPassConsts.m_WorldToViewNoTranslate = mainView.m_WorldToView;
            mainPassConsts.m_WorldToViewNoTranslate.Translation(Vector3::Zero);

            mainPassConsts.m_Quality = m_XeGTAOSettings.QualityLevel;

            nvrhi::TextureHandle debugOutputTexture = g_CommonResources.DummyUAV2DTexture.m_NVRHITextureHandle;
            if (m_DebugOutputMode != 0)
            {
                debugOutputTexture = renderGraph.GetTexture(m_DebugOutputRDGTextureHandle);

                commandList->clearTextureFloat(debugOutputTexture, nvrhi::AllSubresources, nvrhi::Color{});
            }

            nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
                nvrhi::BindingSetItem::PushConstants(1, sizeof(mainPassConsts)),
                nvrhi::BindingSetItem::Texture_SRV(0, workingDepthBuffer),
                nvrhi::BindingSetItem::Texture_SRV(1, m_HilbertLUT),
                nvrhi::BindingSetItem::Texture_SRV(2, GBufferATexture),
                nvrhi::BindingSetItem::Texture_UAV(0, workingSSAOTexture),
                nvrhi::BindingSetItem::Texture_UAV(1, workingEdgesTexture),
                nvrhi::BindingSetItem::Texture_UAV(2, debugOutputTexture),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = StringFormat("ambientocclusion_CS_XeGTAO_MainPass DEBUG_OUTPUT_MODE=%d", m_DebugOutputMode);
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector2U{ workingSSAOTexture->getDesc().width, workingSSAOTexture->getDesc().height }, Vector2U{ XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y });
            computePassParams.m_PushConstantsData = &mainPassConsts;
            computePassParams.m_PushConstantsBytes = sizeof(mainPassConsts);

            g_Graphic.AddComputePass(computePassParams);
        }

        nvrhi::TextureHandle ssaoTexture = renderGraph.GetTexture(g_SSAORDGTextureHandle);

        nvrhi::TextureHandle pingPongTextures[2] = { workingSSAOTexture, ssaoTexture };

        // denoise
        const uint32_t nbPasses = std::max(1, m_XeGTAOSettings.DenoisePasses); // even without denoising we have to run a single last pass to output correct term into the external output texture
        for (size_t i = 0; i < nbPasses; i++)
        {
            const bool bLastPass = (i == nbPasses - 1);

            XeGTAODenoiseConstants denoiseConsts{};
            denoiseConsts.m_FinalApply = bLastPass;

            nvrhi::TextureHandle srcTexture = pingPongTextures[0];
            nvrhi::TextureHandle dstTexture = pingPongTextures[1];
            
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
                nvrhi::BindingSetItem::PushConstants(1, sizeof(denoiseConsts)),
                nvrhi::BindingSetItem::Texture_SRV(0, srcTexture),
                nvrhi::BindingSetItem::Texture_SRV(1, workingEdgesTexture),
                nvrhi::BindingSetItem::Texture_UAV(0, dstTexture),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.PointClampSampler)
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "ambientocclusion_CS_XeGTAO_Denoise";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector2U{ srcTexture->getDesc().width, srcTexture->getDesc().height }, Vector2U{ XE_GTAO_NUMTHREADS_X * 2, XE_GTAO_NUMTHREADS_Y });
            computePassParams.m_PushConstantsData = &denoiseConsts;
            computePassParams.m_PushConstantsBytes = sizeof(denoiseConsts);

            g_Graphic.AddComputePass(computePassParams);

            std::swap(pingPongTextures[0], pingPongTextures[1]);
        }
    }
};

static AmbientOcclusionRenderer gs_AmbientOcclusionRenderer;
IRenderer* g_AmbientOcclusionRenderer = &gs_AmbientOcclusionRenderer;
