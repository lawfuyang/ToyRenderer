#include "Graphic.h"

#include "extern/nvidia/NRD/Include/NRD.h"
#include "extern/imgui/imgui.h"

#include "CommonResources.h"
#include "Engine.h"
#include "FFXHelpers.h"
#include "Scene.h"
#include "RenderGraph.h"

#include "shaders/ShaderInterop.h"

#define NRD_FUNC_CALL(fn) if (nrd::Result result = fn; result != nrd::Result::SUCCESS) { LOG_DEBUG("NRD call failed: %s", EnumUtils::ToString(result)); assert(0); }
#define NRD_ID(x) nrd::Identifier(nrd::Denoiser::x)

RenderGraph::ResourceHandle g_ShadowMaskRDGTextureHandle;
RenderGraph::ResourceHandle g_LinearViewDepthRDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthBufferCopyRDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_GBufferMotionRDGTextureHandle;

static nvrhi::Format GetNVRHIFormat(nrd::Format format)
{
    switch (format)
    {
    case nrd::Format::R8_UNORM:             return nvrhi::Format::R8_UNORM;
    case nrd::Format::R8_SNORM:             return nvrhi::Format::R8_SNORM;
    case nrd::Format::R8_UINT:              return nvrhi::Format::R8_UINT;
    case nrd::Format::R8_SINT:              return nvrhi::Format::R8_SINT;
    case nrd::Format::RG8_UNORM:            return nvrhi::Format::RG8_UNORM;
    case nrd::Format::RG8_SNORM:            return nvrhi::Format::RG8_SNORM;
    case nrd::Format::RG8_UINT:             return nvrhi::Format::RG8_UINT;
    case nrd::Format::RG8_SINT:             return nvrhi::Format::RG8_SINT;
    case nrd::Format::RGBA8_UNORM:          return nvrhi::Format::RGBA8_UNORM;
    case nrd::Format::RGBA8_SNORM:          return nvrhi::Format::RGBA8_SNORM;
    case nrd::Format::RGBA8_UINT:           return nvrhi::Format::RGBA8_UINT;
    case nrd::Format::RGBA8_SINT:           return nvrhi::Format::RGBA8_SINT;
    case nrd::Format::RGBA8_SRGB:           return nvrhi::Format::SRGBA8_UNORM;
    case nrd::Format::R16_UNORM:            return nvrhi::Format::R16_UNORM;
    case nrd::Format::R16_SNORM:            return nvrhi::Format::R16_SNORM;
    case nrd::Format::R16_UINT:             return nvrhi::Format::R16_UINT;
    case nrd::Format::R16_SINT:             return nvrhi::Format::R16_SINT;
    case nrd::Format::R16_SFLOAT:           return nvrhi::Format::R16_FLOAT;
    case nrd::Format::RG16_UNORM:           return nvrhi::Format::RG16_UNORM;
    case nrd::Format::RG16_SNORM:           return nvrhi::Format::RG16_SNORM;
    case nrd::Format::RG16_UINT:            return nvrhi::Format::RG16_UINT;
    case nrd::Format::RG16_SINT:            return nvrhi::Format::RG16_SINT;
    case nrd::Format::RG16_SFLOAT:          return nvrhi::Format::RG16_FLOAT;
    case nrd::Format::RGBA16_UNORM:         return nvrhi::Format::RGBA16_UNORM;
    case nrd::Format::RGBA16_SNORM:         return nvrhi::Format::RGBA16_SNORM;
    case nrd::Format::RGBA16_UINT:          return nvrhi::Format::RGBA16_UINT;
    case nrd::Format::RGBA16_SINT:          return nvrhi::Format::RGBA16_SINT;
    case nrd::Format::RGBA16_SFLOAT:        return nvrhi::Format::RGBA16_FLOAT;
    case nrd::Format::R32_UINT:             return nvrhi::Format::R32_UINT;
    case nrd::Format::R32_SINT:             return nvrhi::Format::R32_SINT;
    case nrd::Format::R32_SFLOAT:           return nvrhi::Format::R32_FLOAT;
    case nrd::Format::RG32_UINT:            return nvrhi::Format::RG32_UINT;
    case nrd::Format::RG32_SINT:            return nvrhi::Format::RG32_SINT;
    case nrd::Format::RG32_SFLOAT:          return nvrhi::Format::RG32_FLOAT;
    case nrd::Format::RGB32_UINT:           return nvrhi::Format::RGB32_UINT;
    case nrd::Format::RGB32_SINT:           return nvrhi::Format::RGB32_SINT;
    case nrd::Format::RGB32_SFLOAT:         return nvrhi::Format::RGB32_FLOAT;
    case nrd::Format::RGBA32_UINT:          return nvrhi::Format::RGBA32_UINT;
    case nrd::Format::RGBA32_SINT:          return nvrhi::Format::RGBA32_SINT;
    case nrd::Format::RGBA32_SFLOAT:        return nvrhi::Format::RGBA32_FLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM: return nvrhi::Format::R10G10B10A2_UNORM;
    case nrd::Format::R10_G10_B10_A2_UINT:  return nvrhi::Format::UNKNOWN; // not representable and not used
    case nrd::Format::R11_G11_B10_UFLOAT:   return nvrhi::Format::R11G11B10_FLOAT;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:   return nvrhi::Format::UNKNOWN; // not representable and not used
    default:                                return nvrhi::Format::UNKNOWN;
    }
}

class ShadowMaskRenderer : public IRenderer
{
    nrd::Instance* m_NRDInstance = nullptr;
    nvrhi::BufferHandle m_NRDConstantBuffer;
    nvrhi::SamplerHandle m_NRDSamplers[(uint32_t)nrd::Sampler::MAX_NUM];
    std::vector<nvrhi::TextureDesc> m_NRDTemporaryTextureDescs;
    std::vector<nvrhi::TextureHandle> m_NRDPermanentTextures;
    std::vector<RenderGraph::ResourceHandle> m_NRDTemporaryTextureHandles;
    RenderGraph::ResourceHandle m_ShadowPenumbraRDGTextureHandle;
    RenderGraph::ResourceHandle m_NormalRoughnessRDGTextureHandle;

    bool m_bEnableSoftShadows = true;
    bool m_bEnableShadowDenoising = true;
    float m_SunAngularDiameter = 0.533f; // empirical shit

public:
    ShadowMaskRenderer() : IRenderer("ShadowMaskRenderer") {}

    ~ShadowMaskRenderer()
    {
        if (m_NRDInstance)
        {
            nrd::DestroyInstance(*m_NRDInstance);
            m_NRDInstance = nullptr;
        }
    }

    void Initialize() override
    {
        if (!g_Graphic.m_Scene->IsRTGIEnabled())
        {
            return;
        }

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();
        // TODO: LOG

        const nrd::DenoiserDesc denoiserDescs[] = { NRD_ID(SIGMA_SHADOW), nrd::Denoiser::SIGMA_SHADOW };

        nrd::InstanceCreationDesc instanceCreationDesc{};
        instanceCreationDesc.denoisers = denoiserDescs;
        instanceCreationDesc.denoisersNum = std::size(denoiserDescs);

        NRD_FUNC_CALL(nrd::CreateInstance(instanceCreationDesc, m_NRDInstance));

        const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_NRDInstance);

        const nvrhi::BufferDesc constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(instanceDesc.constantBufferMaxDataSize, "NrdConstantBuffer", 1);
        m_NRDConstantBuffer = device->createBuffer(constantBufferDesc);

        assert(instanceDesc.samplersNum == std::size(m_NRDSamplers));
        for (uint32_t i = 0; i < std::size(m_NRDSamplers); ++i)
        {
            const nrd::Sampler& samplerMode = instanceDesc.samplers[i];

            switch (samplerMode)
            {
            case nrd::Sampler::NEAREST_CLAMP:
                m_NRDSamplers[i] = g_CommonResources.PointClampSampler;
                break;
            case nrd::Sampler::LINEAR_CLAMP:
                m_NRDSamplers[i] = g_CommonResources.LinearClampSampler;
                break;
            default:
                assert(!"Unknown NRD sampler mode");
                break;
            }
        }

        m_NRDTemporaryTextureHandles.resize(instanceDesc.transientPoolSize);

        const uint32_t poolSize = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

        for (uint32_t i = 0; i < poolSize; i++)
        {
            const bool bIsPermanent = (i < instanceDesc.permanentPoolSize);

            const nrd::TextureDesc& nrdTextureDesc = bIsPermanent
                ? instanceDesc.permanentPool[i]
                : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];

            const nvrhi::Format format = GetNVRHIFormat(nrdTextureDesc.format);
            assert(format != nvrhi::Format::UNKNOWN);

            nvrhi::TextureDesc textureDesc;
            textureDesc.width = DivideAndRoundUp(g_Graphic.m_RenderResolution.x, nrdTextureDesc.downsampleFactor);
            textureDesc.height = DivideAndRoundUp(g_Graphic.m_RenderResolution.y, nrdTextureDesc.downsampleFactor);
            textureDesc.format = format;
            textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
            textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            textureDesc.isUAV = true;
            textureDesc.debugName = StringFormat("NRD %s Texture [%d]", bIsPermanent ? "Permanent" : "Transient", i);

            if (bIsPermanent)
            {
                m_NRDPermanentTextures.push_back(device->createTexture(textureDesc));
            }
            else
            {
                m_NRDTemporaryTextureDescs.push_back(textureDesc);
            }
        }
    }

    void UpdateImgui() override
    {
        ImGui::Checkbox("Enable Shadows", &g_Scene->m_bEnableShadows);
        ImGui::Checkbox("Enable Soft Shadows", &m_bEnableSoftShadows);
        ImGui::Checkbox("Enable Shadow Denoising", &m_bEnableShadowDenoising);
        ImGui::SliderFloat("Sun Angular Diameter", &m_SunAngularDiameter, 0.0f, 3.0f);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Scene->IsShadowsEnabled())
        {
            return false;
        }

        if (!g_Scene->IsRTGIEnabled())
        {
            return false;
        }

        {
            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.format = nvrhi::Format::R8_UNORM;
            desc.debugName = "Shadow Mask Texture";
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(g_ShadowMaskRDGTextureHandle, desc);
        }

        if (m_bEnableShadowDenoising)
        {
            {
                nvrhi::TextureDesc desc;
                desc.width = g_Graphic.m_RenderResolution.x;
                desc.height = g_Graphic.m_RenderResolution.y;
                desc.format = nvrhi::Format::R16_FLOAT;
                desc.debugName = "Shadow Penumbra Texture";
                desc.isUAV = true;
                desc.initialState = nvrhi::ResourceStates::ShaderResource;
                renderGraph.CreateTransientResource(m_ShadowPenumbraRDGTextureHandle, desc);
            }

            {
                nvrhi::TextureDesc desc;
                desc.width = g_Graphic.m_RenderResolution.x;
                desc.height = g_Graphic.m_RenderResolution.y;
                desc.format = nvrhi::Format::R10G10B10A2_UNORM;
                desc.debugName = "Normal Roughness Texture";
                desc.isUAV = true;
                desc.initialState = nvrhi::ResourceStates::ShaderResource;
                renderGraph.CreateTransientResource(m_NormalRoughnessRDGTextureHandle, desc);
            }

            for (uint32_t i = 0; i < m_NRDTemporaryTextureDescs.size(); ++i)
            {
                renderGraph.CreateTransientResource(m_NRDTemporaryTextureHandles[i], m_NRDTemporaryTextureDescs[i]);
            }
        }

        {
            nvrhi::TextureDesc desc;
            desc.width = g_Graphic.m_RenderResolution.x;
            desc.height = g_Graphic.m_RenderResolution.y;
            desc.format = nvrhi::Format::R16_FLOAT;
            desc.debugName = "Linear View Depth";
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(g_LinearViewDepthRDGTextureHandle, desc);
        }

        renderGraph.AddReadDependency(g_DepthBufferCopyRDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferARDGTextureHandle);
        renderGraph.AddReadDependency(g_GBufferMotionRDGTextureHandle);

        return true;
    }

    void TraceShadows(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, "TraceShadows");

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::TextureHandle shadowDataTexture = m_bEnableShadowDenoising ? renderGraph.GetTexture(m_ShadowPenumbraRDGTextureHandle) : renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);
        nvrhi::TextureHandle linearViewDepthTexture = renderGraph.GetTexture(g_LinearViewDepthRDGTextureHandle);
        nvrhi::TextureHandle depthBufferCopy = renderGraph.GetTexture(g_DepthBufferCopyRDGTextureHandle);
        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);

        const float tanSunAngularRadius = tanf(ConvertToRadians(m_SunAngularDiameter * 0.5f));

        ShadowMaskConsts passConstants;
        passConstants.m_ClipToWorld = g_Scene->m_View.m_ClipToWorld;
        passConstants.m_DirectionalLightDirection = g_Scene->m_DirLightVec;
        passConstants.m_OutputResolution = Vector2U{ shadowDataTexture->getDesc().width , shadowDataTexture->getDesc().height };
        passConstants.m_NoisePhase = (g_Graphic.m_FrameCounter & 0xff) * kGoldenRatio;
        passConstants.m_TanSunAngularRadius = m_bEnableSoftShadows ? tanSunAngularRadius : 0.0f;
        passConstants.m_CameraPosition = g_Scene->m_View.m_Eye;
        passConstants.m_bDoDenoising = m_bEnableShadowDenoising;
        passConstants.m_RayStartOffset = (g_Scene->m_BoundingSphere.Radius < 3.0f) ? 0.01f : 0.1f;
        passConstants.m_GlobalVertexBufferIdxInHeap = g_Graphic.m_GlobalVertexBuffer->indexInHeap;
        passConstants.m_GlobalIndexBufferIdxInHeap = g_Graphic.m_GlobalIndexBuffer->indexInHeap;
        passConstants.m_GlobalMeshDataBufferIdxInHeap = g_Graphic.m_GlobalMeshDataBuffer->indexInHeap;

        nvrhi::BufferHandle passConstantBuffer = g_Graphic.CreateConstantBuffer(commandList, passConstants);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, passConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(ShadowMaskResourceIndices)),
            nvrhi::BindingSetItem::Texture_SRV(0, depthBufferCopy),
            nvrhi::BindingSetItem::RayTracingAccelStruct(1, g_Scene->m_TLAS),
            nvrhi::BindingSetItem::Texture_SRV(2, GBufferATexture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, g_Graphic.m_GlobalVertexBuffer), // TODO: remove after bindless refactoring
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Graphic.m_GlobalMaterialDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalIndexBuffer), // TODO: remove after bindless refactoring
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, g_Graphic.m_GlobalMeshDataBuffer), // TODO: remove after bindless refactoring
            nvrhi::BindingSetItem::Texture_SRV(8, g_CommonResources.BlueNoise.m_NVRHITextureHandle),
            nvrhi::BindingSetItem::Texture_UAV(0, shadowDataTexture),
            nvrhi::BindingSetItem::Texture_UAV(1, linearViewDepthTexture),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler),
        };

        nvrhi::BindingSetHandle bindingSet;
        nvrhi::BindingLayoutHandle bindingLayout;
        g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

        ShadowMaskResourceIndices resourceIndices;
        resourceIndices.m_DepthBufferIdx = bindingSet->m_ResourceDescriptorHeapStartIdx;
        resourceIndices.m_SceneTLASIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 1;
        resourceIndices.m_GBufferAIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 2;
        resourceIndices.m_BasePassInstanceConstsIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 3;
        resourceIndices.m_MaterialDataBufferIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 5;
        resourceIndices.m_BlueNoiseIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 8;
        resourceIndices.m_ShadowDataOutputIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 9;
        resourceIndices.m_LinearViewDepthOutputIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 10;
        resourceIndices.m_SamplersIdx = bindingSet->m_SamplerDescriptorHeapStartIdx;

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "shadowmask_CS_ShadowMask";
        computePassParams.m_BindingSets = { bindingSet, g_Graphic.GetSrvUavCbvDescriptorTable() };
        computePassParams.m_BindingLayouts = { bindingLayout, g_Graphic.m_SrvUavCbvBindlessLayout };
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(passConstants.m_OutputResolution, 8);
        computePassParams.m_PushConstantsData = &resourceIndices;
        computePassParams.m_PushConstantsBytes = sizeof(resourceIndices);

        g_Graphic.AddComputePass(computePassParams);
    }

    void PackNormalRoughness(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        nvrhi::TextureHandle GBufferATexture = renderGraph.GetTexture(g_GBufferARDGTextureHandle);
        nvrhi::TextureHandle normalRoughnessTexture = renderGraph.GetTexture(m_NormalRoughnessRDGTextureHandle);

        PackNormalAndRoughnessConsts passConstants;
        passConstants.m_OutputResolution = Vector2U{ normalRoughnessTexture->getDesc().width, normalRoughnessTexture->getDesc().height };

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
            nvrhi::BindingSetItem::Texture_SRV(0, GBufferATexture),
            nvrhi::BindingSetItem::Texture_UAV(0, normalRoughnessTexture)
        };

        nvrhi::BindingSetHandle bindingSet;
        nvrhi::BindingLayoutHandle bindingLayout;
        g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

        passConstants.m_GBufferAIdx = bindingSet->m_ResourceDescriptorHeapStartIdx;
        passConstants.m_NormalRoughnessOutputIdx = bindingSet->m_ResourceDescriptorHeapStartIdx + 1;

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "shadowmask_CS_PackNormalAndRoughness";
        computePassParams.m_BindingSets = { bindingSet };
        computePassParams.m_BindingLayouts = { bindingLayout };
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(passConstants.m_OutputResolution, 8);
        computePassParams.m_PushConstantsData = &passConstants;
        computePassParams.m_PushConstantsBytes = sizeof(passConstants);

        g_Graphic.AddComputePass(computePassParams);
    }

    void DenoiseShadows(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        if (!m_bEnableShadowDenoising)
        {
            return;
        }

        PROFILE_GPU_SCOPED(commandList, "Denoise Shadows");

        // TODO: when we have multiple passes using NRD, we can move this to its own renderer
        PackNormalRoughness(commandList, renderGraph);

        nrd::SigmaSettings sigmaSettings;
        memcpy(sigmaSettings.lightDirection, &g_Scene->m_DirLightVec, sizeof(sigmaSettings.lightDirection));
        NRD_FUNC_CALL(nrd::SetDenoiserSettings(*m_NRDInstance, NRD_ID(SIGMA_SHADOW), &sigmaSettings));

        nrd::CommonSettings commonSettings;
        memcpy(commonSettings.viewToClipMatrix, &g_Scene->m_View.m_ViewToClip, sizeof(commonSettings.viewToClipMatrix));
        memcpy(commonSettings.viewToClipMatrixPrev, &g_Scene->m_View.m_PrevViewToClip, sizeof(commonSettings.viewToClipMatrixPrev));
        memcpy(commonSettings.worldToViewMatrix, &g_Scene->m_View.m_WorldToView, sizeof(commonSettings.worldToViewMatrix));
        memcpy(commonSettings.worldToViewMatrixPrev, &g_Scene->m_View.m_PrevWorldToView, sizeof(commonSettings.worldToViewMatrixPrev));
        commonSettings.motionVectorScale[0] = 1.0f / g_Graphic.m_RenderResolution.x;
        commonSettings.motionVectorScale[1] = 1.0f / g_Graphic.m_RenderResolution.y;
        commonSettings.cameraJitter[0] = 0.0f; // TODO: jitter stuff
        commonSettings.cameraJitter[1] = 0.0f;
        commonSettings.cameraJitterPrev[0] = 0.0f;
        commonSettings.cameraJitterPrev[1] = 0.0f;
        commonSettings.resourceSize[0] = g_Graphic.m_RenderResolution.x;
        commonSettings.resourceSize[1] = g_Graphic.m_RenderResolution.y;
        commonSettings.resourceSizePrev[0] = g_Graphic.m_RenderResolution.x;
        commonSettings.resourceSizePrev[1] = g_Graphic.m_RenderResolution.y;
        commonSettings.rectSize[0] = g_Graphic.m_DisplayResolution.x;
        commonSettings.rectSize[1] = g_Graphic.m_DisplayResolution.y;
        commonSettings.rectSizePrev[0] = g_Graphic.m_DisplayResolution.x;
        commonSettings.rectSizePrev[1] = g_Graphic.m_DisplayResolution.y;
        commonSettings.denoisingRange = std::max(100.0f, g_Scene->m_BoundingSphere.Radius * 2);
        commonSettings.frameIndex = g_Graphic.m_FrameCounter;
        commonSettings.accumulationMode = nrd::AccumulationMode::CONTINUE; // TODO: change when camera resets or jumps
        commonSettings.isMotionVectorInWorldSpace = false;
        commonSettings.enableValidation = false; // NOTE: not used for SIGMA denoising

        NRD_FUNC_CALL(nrd::SetCommonSettings(*m_NRDInstance, commonSettings));

        const nrd::Identifier denoiserIdentifiers[] = { NRD_ID(SIGMA_SHADOW) };
        const nrd::DispatchDesc* dispatchDescs = nullptr;
        uint32_t dispatchDescNum = 0;
        NRD_FUNC_CALL(nrd::GetComputeDispatches(*m_NRDInstance, denoiserIdentifiers, std::size(denoiserIdentifiers), dispatchDescs, dispatchDescNum));

        std::vector<nvrhi::TextureHandle> transientTextures;
        transientTextures.resize(m_NRDTemporaryTextureHandles.size());

        for (uint32_t i = 0; i < m_NRDTemporaryTextureHandles.size(); ++i)
        {
            transientTextures[i] = renderGraph.GetTexture(m_NRDTemporaryTextureHandles[i]);
        }

        nvrhi::TextureHandle linearViewDepthTexture = renderGraph.GetTexture(g_LinearViewDepthRDGTextureHandle);
        nvrhi::TextureHandle GBufferMotionTexture = renderGraph.GetTexture(g_GBufferMotionRDGTextureHandle);
        nvrhi::TextureHandle shadowPenumbraTexture = renderGraph.GetTexture(m_ShadowPenumbraRDGTextureHandle);
        nvrhi::TextureHandle normalRoughnessTexture = renderGraph.GetTexture(m_NormalRoughnessRDGTextureHandle);
        nvrhi::TextureHandle shadowTranslucencyTexture = renderGraph.GetTexture(g_ShadowMaskRDGTextureHandle);

        const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_NRDInstance);

        // NVRHI will check that volatile buffers be written to first before being bound
        {
            std::vector<std::byte> dummyConstantBufferBytes;
            dummyConstantBufferBytes.resize(instanceDesc.constantBufferMaxDataSize);

            commandList->writeBuffer(m_NRDConstantBuffer, dummyConstantBufferBytes.data(), dummyConstantBufferBytes.size());
        }

        for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchDescNum; dispatchIndex++)
        {
            const nrd::DispatchDesc& dispatchDesc = dispatchDescs[dispatchIndex];
            const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];

            if (dispatchDesc.constantBufferDataSize)
            {
                commandList->writeBuffer(m_NRDConstantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);
            }

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(instanceDesc.constantBufferRegisterIndex, m_NRDConstantBuffer));

            uint32_t resourceIndex = 0;
            for (uint32_t resourceRangeIndex = 0; resourceRangeIndex < nrdPipelineDesc.resourceRangesNum; resourceRangeIndex++)
            {
                const nrd::ResourceRangeDesc& nrdDescriptorRange = nrdPipelineDesc.resourceRanges[resourceRangeIndex];

                for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorsNum; descriptorOffset++)
                {
                    assert(resourceIndex < dispatchDesc.resourcesNum);
                    const nrd::ResourceDesc& resource = dispatchDesc.resources[resourceIndex];

                    nvrhi::TextureHandle texture;
                    switch (resource.type)
                    {
                    case nrd::ResourceType::IN_MV:
                        texture = GBufferMotionTexture;
                        break;
                    case nrd::ResourceType::IN_VIEWZ:
                        texture = linearViewDepthTexture;
                        break;
                    case nrd::ResourceType::IN_PENUMBRA:
                        texture = shadowPenumbraTexture;
                        break;
                    case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
                        texture = normalRoughnessTexture;
                        break;
                    case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:
                        texture = shadowTranslucencyTexture;
                        break;
                    case nrd::ResourceType::TRANSIENT_POOL:
                        texture = transientTextures[resource.indexInPool];
                        break;
                    case nrd::ResourceType::PERMANENT_POOL:
                        texture = m_NRDPermanentTextures[resource.indexInPool];
                        break;
                    default:
                        assert(!"Unhandled NRD resource type");
                        break;
                    }

                    assert(texture);

                    nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources;
                    subresources.baseMipLevel = 0;
                    subresources.numMipLevels = 1;

                    nvrhi::BindingSetItem setItem = nvrhi::BindingSetItem::None();
                    setItem.resourceHandle = texture;
                    setItem.slot = instanceDesc.resourcesBaseRegisterIndex + descriptorOffset;
                    setItem.subresources = subresources;
                    setItem.type = (nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE)
                        ? nvrhi::ResourceType::Texture_SRV
                        : nvrhi::ResourceType::Texture_UAV;

                    bindingSetDesc.bindings.push_back(setItem);

                    resourceIndex++;
                }
            }
            assert(resourceIndex == dispatchDesc.resourcesNum);

            nvrhi::BindingSetHandle bindingSet;
            nvrhi::BindingLayoutHandle bindingLayout;
            g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = nrdPipelineDesc.shaderFileName;
            computePassParams.m_BindingSets = { bindingSet };
            computePassParams.m_BindingLayouts = { bindingLayout };
            computePassParams.m_DispatchGroupSize = {dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1};

            if (instanceDesc.samplersNum > 0)
            {
                nvrhi::BindingSetDesc samplersBindingSetDesc;
                assert(instanceDesc.samplersNum <= std::size(m_NRDSamplers));
                for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc.samplersNum; samplerIndex++)
                {
                    samplersBindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(instanceDesc.samplersBaseRegisterIndex + samplerIndex, m_NRDSamplers[samplerIndex]));
                }

                nvrhi::BindingSetHandle samplersBindingSet;
                nvrhi::BindingLayoutHandle samplersBindingLayout;
                g_Graphic.CreateBindingSetAndLayout(samplersBindingSetDesc, samplersBindingSet, samplersBindingLayout, 1); // NOTE: samplers are in register space 1, as of NRD 4.15

                computePassParams.m_BindingSets.push_back(samplersBindingSet);
                computePassParams.m_BindingLayouts.push_back(samplersBindingLayout);
            }

            g_Graphic.AddComputePass(computePassParams);
        }
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        TraceShadows(commandList, renderGraph);
        DenoiseShadows(commandList, renderGraph);
    }
};

static ShadowMaskRenderer gs_ShadowMaskRenderer;
IRenderer* g_ShadowMaskRenderer = &gs_ShadowMaskRenderer;
