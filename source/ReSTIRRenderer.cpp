#include "Graphic.h"

#include "Scene.h"
#include "RenderGraph.h"

#include "Rtxdi/ImportanceSamplingContext.h"

#include "shaders/RtxdiShaderInterop.h"

RenderGraph::ResourceHandle g_ReSTIRShadingOutputRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;

class ReSTIRRenderer : public IRenderer
{
    std::unique_ptr<rtxdi::ImportanceSamplingContext> m_ImportanceSamplingContext;

    nvrhi::BufferHandle m_LightDataBuffer;
    nvrhi::BufferHandle m_LightReservoirBuffer;

public:
    ReSTIRRenderer()
        : IRenderer("Importance Sampling Renderer")
    {
    }

    ~ReSTIRRenderer() override
    {
        m_ImportanceSamplingContext.reset();
    }

    void Initialize() override
    {
        rtxdi::ImportanceSamplingContext_StaticParameters isParams;
        isParams.renderWidth = g_Graphic.m_RenderResolution.x;
        isParams.renderHeight = g_Graphic.m_RenderResolution.y;

        m_ImportanceSamplingContext = std::make_unique<rtxdi::ImportanceSamplingContext>(isParams);

        nvrhi::BufferDesc lightBufferDesc;
        lightBufferDesc.byteSize = sizeof(ReSTIRLightInfo); // TODO: dir light only for now. add emissive triangles after scene load
        lightBufferDesc.structStride = sizeof(ReSTIRLightInfo);
        lightBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        lightBufferDesc.debugName = "ReSTIR Light Info Buffer";
        //lightBufferDesc.canHaveUAVs = true; // TODO: uncomment this when we have to populate this buffer in a CS for emissive triangles
        m_LightDataBuffer = g_Graphic.m_NVRHIDevice->createBuffer(lightBufferDesc);

        nvrhi::BufferDesc lightReservoirBufferDesc;
        lightReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedDIReservoir) * m_ImportanceSamplingContext->GetReSTIRDIContext().GetReservoirBufferParameters().reservoirArrayPitch * rtxdi::c_NumReSTIRDIReservoirBuffers;
        lightReservoirBufferDesc.structStride = sizeof(RTXDI_PackedDIReservoir);
        lightReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        lightReservoirBufferDesc.keepInitialState = true;
        lightReservoirBufferDesc.debugName = "LightReservoirBuffer";
        lightReservoirBufferDesc.canHaveUAVs = true;
        m_LightReservoirBuffer = g_Graphic.m_NVRHIDevice->createBuffer(lightReservoirBufferDesc);
    }

    bool HasImguiControls() const { return false; }

    void UpdateImgui() override
    {

    }

    bool Setup(RenderGraph& renderGraph) override
    {
        renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferMotionRDGTextureHandle);
        renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);

        nvrhi::TextureDesc restirOutputDesc;
        restirOutputDesc.width = g_Graphic.m_RenderResolution.x;
        restirOutputDesc.height = g_Graphic.m_RenderResolution.y;
        restirOutputDesc.format = nvrhi::Format::R11G11B10_FLOAT;
        restirOutputDesc.isUAV = true;
        restirOutputDesc.debugName = "ReSTIR Shading Output Texture";
        renderGraph.CreateTransientResource(g_ReSTIRShadingOutputRDGTextureHandle, restirOutputDesc);

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        // TODO: add emissive triangles & local lights
        RTXDI_LightBufferParameters lightBufferParams{};
        lightBufferParams.localLightBufferRegion.firstLightIndex = RTXDI_INVALID_LIGHT_INDEX;
        lightBufferParams.localLightBufferRegion.numLights = 0;
        lightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0;
        lightBufferParams.infiniteLightBufferRegion.numLights = 1;
        lightBufferParams.environmentLightParams.lightIndex = RTXDI_INVALID_LIGHT_INDEX;
        lightBufferParams.environmentLightParams.lightPresent = false;
        
        ReSTIRLightingConstants restirLightingConstants;
        restirLightingConstants.m_LightBufferParams = lightBufferParams;
        restirLightingConstants.m_ReservoirBufferParams = m_ImportanceSamplingContext->GetReSTIRDIContext().GetReservoirBufferParameters();
        restirLightingConstants.m_ClipToWorld = g_Scene->m_View.m_ClipToWorld;
        restirLightingConstants.m_CameraPosition = g_Scene->m_View.m_Eye;
        restirLightingConstants.m_InputBufferIndex = 0;
        restirLightingConstants.m_OutputResolutionInv = Vector2{ 1.0f / g_Graphic.m_RenderResolution.x, 1.0f / g_Graphic.m_RenderResolution.y };
        restirLightingConstants.m_OutputBufferIndex = 0;

        nvrhi::BufferHandle constantBuffer = g_Graphic.CreateConstantBuffer(commandList, restirLightingConstants);

        ReSTIRLightInfo dirLightInfo;
        dirLightInfo.m_Direction = g_Scene->m_DirLightVec;
        dirLightInfo.m_Radiance = Vector3{ g_Scene->m_DirLightStrength };
        commandList->writeBuffer(m_LightDataBuffer, &dirLightInfo, sizeof(ReSTIRLightInfo));

        nvrhi::TextureHandle gbufferA = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle gbufferMotion = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
        nvrhi::TextureHandle depthBuffer = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        nvrhi::TextureHandle restirShadingOutput = renderGraph.GetTexture(g_ReSTIRShadingOutputRDGTextureHandle);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, constantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, g_Scene->m_TLAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_LightDataBuffer),
            nvrhi::BindingSetItem::Texture_SRV(2, gbufferA),
            nvrhi::BindingSetItem::Texture_SRV(3, gbufferMotion),
            nvrhi::BindingSetItem::Texture_SRV(4, depthBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_LightReservoirBuffer),
            nvrhi::BindingSetItem::Texture_UAV(1, restirShadingOutput),
        };

        Graphic::ComputePassParams passParams;
        passParams.m_CommandList = commandList;
        passParams.m_ShaderName = "restirshading_CS_Main";
        passParams.m_BindingSetDesc = bindingSetDesc;
        passParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(g_Graphic.m_RenderResolution, Vector2U{ 8, 8 });
        g_Graphic.AddComputePass(passParams);
    }
};

DEFINE_RENDERER(ReSTIRRenderer);
