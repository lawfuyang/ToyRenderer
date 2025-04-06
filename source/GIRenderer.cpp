#include "Graphic.h"

#include "extern/imgui/imgui.h"
#include "rtxgi/ddgi/DDGIVolume.h"
#include "shaders/DDGIShaderConfig.h"

#include "CommonResources.h"
#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Utilities.h"

#include "shaders/ShaderInterop.h"

static_assert(RTXGI_DDGI_WAVE_LANE_COUNT == kNumThreadsPerWave);

extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;
RenderGraph::ResourceHandle g_DDGIOutputRDGTextureHandle;

static bool gs_bShowDebugProbes = false;
static float gs_MaxDebugProbeDistance = 10.0f;

// dont bother with using rtxgi::d3d12::DDGIVolume. We handle all resources internally. Just re-use their logic.
class GIVolume : public rtxgi::DDGIVolumeBase
{
public:
    void Create()
    {
        assert(GetNumProbes() > 0);

        LOG_DEBUG("Creating GI volume, origin: [%.1f, %.1f, %.1f], num probes: [%u, %u, %u]",
                  m_desc.origin.x, m_desc.origin.y, m_desc.origin.z,
                  m_desc.probeCounts.x, m_desc.probeCounts.y, m_desc.probeCounts.z);

        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::RayData);
        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Irradiance);
        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Distance);
        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Data);
        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Variability);
        CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::VariabilityAverage);

        m_ProbeVariabilityReadback.Initialize(sizeof(float));

        // Store the volume rotation
        m_rotationMatrix = EulerAnglesToRotationMatrix(m_desc.eulerAngles);
        m_rotationQuaternion = RotationMatrixToQuaternion(m_rotationMatrix);

        // Set the default scroll anchor to the origin
        m_probeScrollAnchor = m_desc.origin;

        SeedRNG(std::random_device{}());
    }

    void ClearProbes(nvrhi::CommandListHandle commandList)
    {
        commandList->clearTextureFloat(m_ProbeIrradiance, nvrhi::AllSubresources, m_ProbeIrradiance->getDesc().clearValue);
        commandList->clearTextureFloat(m_ProbeDistance, nvrhi::AllSubresources, m_ProbeDistance->getDesc().clearValue);
    }

    void Destroy() override {}

    rtxgi::DDGIVolumeDesc& GetDesc() { return m_desc; }

    uint32_t GetNumProbes() const { return m_desc.probeCounts.x * m_desc.probeCounts.y * m_desc.probeCounts.z; }

    float GetDebugProbeRadius() const
    {
        return std::max(m_desc.probeSpacing.x, std::max(m_desc.probeSpacing.y, m_desc.probeSpacing.z)) * 0.1f;
    }

    nvrhi::TextureHandle m_ProbeRayData;            // Probe ray data texture array - RGB: radiance | A: hit distance
    nvrhi::TextureHandle m_ProbeIrradiance;         // Probe irradiance texture array - RGB irradiance, encoded with a high gamma curve
    nvrhi::TextureHandle m_ProbeDistance;           // Probe distance texture array - R: mean distance | G: mean distance^2
    nvrhi::TextureHandle m_ProbeData;               // Probe data texture array - XYZ: world-space relocation offsets | W: classification state
    nvrhi::TextureHandle m_ProbeVariability;        // Probe variability texture array
    nvrhi::TextureHandle m_ProbeVariabilityAverage; // Average of Probe variability for whole volume
    FencedReadbackBuffer m_ProbeVariabilityReadback; // CPU-readable resource containing final Probe variability average

private:
    struct ProbeTextureCreateInfo
    {
        nvrhi::TextureHandle& m_TextureHandle;
        const char* m_Name;
        nvrhi::Format m_Format;
        nvrhi::ResourceStates m_InitialState;
        bool m_bIsRenderTarget;
    };

    const ProbeTextureCreateInfo kTextureCreateInfos[(int)rtxgi::EDDGIVolumeTextureType::Count] =
    {
        { m_ProbeRayData, "Probe Ray Data", nvrhi::Format::RG32_FLOAT, nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeIrradiance, "Probe Irradiance", nvrhi::Format::R10G10B10A2_UNORM, nvrhi::ResourceStates::ShaderResource, true },
        { m_ProbeDistance, "Probe Distance", nvrhi::Format::RG16_FLOAT, nvrhi::ResourceStates::ShaderResource, true }, // Note: in large environments FP16 may not be sufficient
        { m_ProbeData, "Probe Data", nvrhi::Format::RGBA16_FLOAT, nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeVariability, "Probe Variability", nvrhi::Format::R16_FLOAT, nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeVariabilityAverage, "Probe Variability Average", nvrhi::Format::R16_FLOAT, nvrhi::ResourceStates::UnorderedAccess, false },
    };

    void CreateProbeTexture(rtxgi::EDDGIVolumeTextureType textureType)
    {
        uint32_t width, height, arraySize;
        GetDDGIVolumeTextureDimensions(m_desc, textureType, width, height, arraySize);
        assert(!(width <= 0 || height <= 0 || arraySize <= 0));

        const ProbeTextureCreateInfo& createInfo = kTextureCreateInfos[(int)textureType];

        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.arraySize = arraySize;
        desc.format = createInfo.m_Format;
        desc.dimension = nvrhi::TextureDimension::Texture2DArray;
        desc.debugName = createInfo.m_Name;
        desc.isRenderTarget = createInfo.m_bIsRenderTarget;
        desc.isUAV = true;
        desc.initialState = createInfo.m_InitialState;

        if (desc.isRenderTarget)
        {
            desc.setClearValue(nvrhi::Color{ 0.0f, 0.0f, 0.0f, 1.0f });
        }

        createInfo.m_TextureHandle = g_Graphic.m_NVRHIDevice->createTexture(desc);

        LOG_DEBUG("Created DDGI volume texture: %s, %ux%u, %u slices, format: %s", desc.debugName.c_str(), desc.width, desc.height, desc.arraySize, nvrhi::utils::FormatToString(desc.format));
    }
};

class GIRenderer : public IRenderer
{
public:
    bool m_bResetProbes = true;
    Vector3 m_ProbeSpacing{ 1.0f, 1.0f, 1.0f };
    uint32_t m_ProbeNumRays = 256;

    nvrhi::BufferHandle m_VolumeDescGPUBuffer;

    GIVolume m_GIVolume;

    GIRenderer() : IRenderer("GIRenderer") {}

    void Initialize() override
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(rtxgi::DDGIVolumeDescGPUPacked); // TODO: multiple volumes
        desc.structStride = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
        desc.debugName = "DDGI Volume Desc GPU Packed";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        m_VolumeDescGPUBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
    }

    void PostSceneLoad() override
    {
        static const int kProbeNumIrradianceTexels = 8;
        static const int kProbeNumDistanceTexels = 16;

        auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        // enforce minimum of 10x10x10 probes
        m_ProbeSpacing.x = std::min(m_ProbeSpacing.x, g_Scene->m_AABB.Extents.x * 0.2f);
        m_ProbeSpacing.y = std::min(m_ProbeSpacing.y, g_Scene->m_AABB.Extents.y * 0.2f);
        m_ProbeSpacing.z = std::min(m_ProbeSpacing.z, g_Scene->m_AABB.Extents.z * 0.2f);

        // enforce maximum of 128 probes per axis
        m_ProbeSpacing.x = std::max(m_ProbeSpacing.x, g_Scene->m_AABB.Extents.x / 64.0f);
        m_ProbeSpacing.y = std::max(m_ProbeSpacing.y, g_Scene->m_AABB.Extents.y / 64.0f);
        m_ProbeSpacing.z = std::max(m_ProbeSpacing.z, g_Scene->m_AABB.Extents.z / 64.0f);

        // XY = horizontal plane, Z = vertical plane
        const rtxgi::int3 volumeProbeCounts =
        {
            (int)std::ceil(g_Scene->m_AABB.Extents.x * 2 / m_ProbeSpacing.x),
            (int)std::ceil(g_Scene->m_AABB.Extents.y * 2 / m_ProbeSpacing.y),
            (int)std::ceil(g_Scene->m_AABB.Extents.z * 2 / m_ProbeSpacing.z)
        };

        rtxgi::DDGIVolumeDesc& volumeDesc = m_GIVolume.GetDesc();
        volumeDesc.origin = rtxgi::float3{ g_Scene->m_AABB.Center.x, g_Scene->m_AABB.Center.y, g_Scene->m_AABB.Center.z };
        volumeDesc.eulerAngles = rtxgi::float3{ 0.0f, 0.0f, 0.0f }; // TODO: OBB?
        volumeDesc.probeSpacing = rtxgi::float3{ m_ProbeSpacing.x, m_ProbeSpacing.y, m_ProbeSpacing.z };
        volumeDesc.probeCounts = volumeProbeCounts;
        volumeDesc.probeNumRays = m_ProbeNumRays;
        volumeDesc.probeNumIrradianceTexels = kProbeNumIrradianceTexels;
        volumeDesc.probeNumIrradianceInteriorTexels = kProbeNumIrradianceTexels - 2;
        volumeDesc.probeNumDistanceTexels = kProbeNumDistanceTexels;
        volumeDesc.probeNumDistanceInteriorTexels = kProbeNumDistanceTexels - 2;
        volumeDesc.probeMaxRayDistance = g_Scene->m_BoundingSphere.Radius; // empirical shit. Just use scene BS radius
        volumeDesc.probeRelocationEnabled = false; // TODO
        volumeDesc.probeClassificationEnabled = false; // TODO
        volumeDesc.probeVariabilityEnabled = false; // TODO
        volumeDesc.movementType = rtxgi::EDDGIVolumeMovementType::Default;
        volumeDesc.probeVisType = rtxgi::EDDGIVolumeProbeVisType::Default;
        
        // leave these values as defaults?
        volumeDesc.probeHysteresis = 0.97f;
        volumeDesc.probeDistanceExponent = 50.f;
        volumeDesc.probeIrradianceEncodingGamma = 5.f;
        volumeDesc.probeIrradianceThreshold = 0.25f;
        volumeDesc.probeBrightnessThreshold = 0.10f;
        volumeDesc.probeRandomRayBackfaceThreshold = 0.1f;
        volumeDesc.probeFixedRayBackfaceThreshold = 0.25f;
        volumeDesc.probeViewBias = 0.1f;
        volumeDesc.probeNormalBias = 0.1f;
        volumeDesc.probeMinFrontfaceDistance = 1.f;

        m_GIVolume.Create();
    }

    void UpdateImgui() override
    {
        auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        ImGui::Checkbox("Enabled", &controllables.m_bEnabled);

        if (!controllables.m_bEnabled)
        {
            return;
        }

        ImGui::Checkbox("Show Debug Probes", &gs_bShowDebugProbes);
        ImGui::Checkbox("Reset Probes", &m_bResetProbes);
        ImGui::DragFloat3("Probe Spacing", (float*)&m_ProbeSpacing, 1.0f, 0.1f, 2.0f);
        ImGui::DragInt("Probe Num Rays", (int*)&m_ProbeNumRays, 1.0f, 128, 512);
        ImGui::DragFloat("Max Debug Probe Distance", &gs_MaxDebugProbeDistance, 1.0f, 10.0f, 1000.0f);
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        const auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        if (!controllables.m_bEnabled)
        {
            return false;
        }

        nvrhi::TextureDesc desc;
        desc.width = g_Graphic.m_RenderResolution.x;
        desc.height = g_Graphic.m_RenderResolution.y;
        desc.format = nvrhi::Format::R11G11B10_FLOAT;
        desc.isUAV = true;
        desc.debugName = "DDGI Output";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        renderGraph.CreateTransientResource(g_DDGIOutputRDGTextureHandle, desc);

        return true;
    }

    void ResetProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        m_GIVolume.ClearProbes(commandList);
        //DDGIProbeRelocationResetCS
        //DDGIProbeClassificationResetCS
    }

    void ReadbackDDGIVolumeVariability(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void RayTraceVolumes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void UpdateDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void RelocateDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void ClassifyDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void CalculateDDGIVolumeVariability(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void GatherIndirectLighting(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::TextureHandle ddgiOutputTexture = renderGraph.GetTexture(g_DDGIOutputRDGTextureHandle);

        if (m_bResetProbes)
        {
            ResetProbes(commandList, renderGraph);
            m_bResetProbes = false;
        }

        m_GIVolume.Update();

        const rtxgi::DDGIVolumeDescGPUPacked volumeDescGPU = m_GIVolume.GetDescGPUPacked();
        commandList->writeBuffer(m_VolumeDescGPUBuffer, &volumeDescGPU, sizeof(rtxgi::DDGIVolumeDescGPUPacked));

        ReadbackDDGIVolumeVariability(commandList, renderGraph);
        RayTraceVolumes(commandList, renderGraph);
        UpdateDDGIVolumeProbes(commandList, renderGraph);
        RelocateDDGIVolumeProbes(commandList, renderGraph);
        ClassifyDDGIVolumeProbes(commandList, renderGraph);
        CalculateDDGIVolumeVariability(commandList, renderGraph);
        GatherIndirectLighting(commandList, renderGraph);
    }
};
static GIRenderer gs_GIRenderer;
IRenderer* g_GIRenderer = &gs_GIRenderer;

class GIDebugRenderer : public IRenderer
{
    RenderGraph::ResourceHandle m_ProbePositionsRDGBufferHandle;
    RenderGraph::ResourceHandle m_ProbeDrawIndirectArgsRDGBufferHandle;

public:
    GIDebugRenderer() : IRenderer("GIDebugRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!gs_bShowDebugProbes)
        {
            return false;
        }

        renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(Vector3) * gs_GIRenderer.m_GIVolume.GetNumProbes();
            desc.structStride = sizeof(Vector3);
            desc.canHaveUAVs = true;
            desc.debugName = "Probe Positions";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;

            renderGraph.CreateTransientResource(m_ProbePositionsRDGBufferHandle, desc);
        }

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(DrawIndexedIndirectArguments);
            desc.structStride = sizeof(DrawIndexedIndirectArguments);
            desc.canHaveUAVs = true;
            desc.isDrawIndirectArgs = true;
            desc.debugName = "Probe Draw Indirect Args";
            desc.initialState = nvrhi::ResourceStates::IndirectArgument;

            renderGraph.CreateTransientResource(m_ProbeDrawIndirectArgsRDGBufferHandle, desc);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::BufferHandle probePositionsBuffer = renderGraph.GetBuffer(m_ProbePositionsRDGBufferHandle);
        nvrhi::BufferHandle probeDrawIndirectArgsBuffer = renderGraph.GetBuffer(m_ProbeDrawIndirectArgsRDGBufferHandle);

        DrawIndexedIndirectArguments indirectArgs{};
        indirectArgs.m_IndexCount = g_CommonResources.UnitSphere.m_NumIndices;
        commandList->writeBuffer(probeDrawIndirectArgsBuffer, &indirectArgs, sizeof(indirectArgs));

        const uint32_t numProbes = gs_GIRenderer.m_GIVolume.GetNumProbes();

        // get probe positions from the volume
        {
            GIProbeVisualizationUpdateConsts updateConsts;
            updateConsts.m_NumProbes = numProbes;
            updateConsts.m_CameraOrigin = g_Scene->m_View.m_Eye;
            updateConsts.m_MaxDebugProbeDistance = gs_MaxDebugProbeDistance;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(updateConsts)),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, probePositionsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(1, probeDrawIndirectArgsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, gs_GIRenderer.m_VolumeDescGPUBuffer),
                nvrhi::BindingSetItem::Texture_SRV(1, gs_GIRenderer.m_GIVolume.m_ProbeData),
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "giprobevisualization_CS_UpdateProbePositions";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(numProbes, kNumThreadsPerWave);
            computePassParams.m_PushConstantsData = &updateConsts;
            computePassParams.m_PushConstantsBytes = sizeof(updateConsts);

            g_Graphic.AddComputePass(computePassParams);
        }

        // draw probes
        {
            PROFILE_GPU_SCOPED(commandList, "Draw Probes");

            nvrhi::TextureHandle depthBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

            nvrhi::FramebufferDesc frameBufferDesc;
            frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());
            frameBufferDesc.setDepthAttachment(depthBuffer)
                .depthAttachment.isReadOnly = true;
            nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

            GIProbeVisualizationConsts passConstants;
            passConstants.m_WorldToClip = g_Scene->m_View.m_WorldToClip;
            passConstants.m_ProbeRadius = gs_GIRenderer.m_GIVolume.GetDebugProbeRadius();

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, probePositionsBuffer),
            };

            nvrhi::BindingSetHandle bindingSet;
            nvrhi::BindingLayoutHandle bindingLayout;
            g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = g_CommonResources.m_UncompressedRawVertexFormatInputLayoutHandle;
            pipelineDesc.VS = g_Graphic.GetShader("giprobevisualization_VS_GIProbes");
            pipelineDesc.PS = g_Graphic.GetShader("giprobevisualization_PS_GIProbes");
            pipelineDesc.renderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthReadStencilNone, g_CommonResources.CullBackFace };
            pipelineDesc.bindingLayouts = { bindingLayout };

            nvrhi::GraphicsState graphicsState;
            graphicsState.pipeline = g_Graphic.GetOrCreatePSO(pipelineDesc, frameBuffer);
            graphicsState.framebuffer = frameBuffer;
            graphicsState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)g_Graphic.m_DisplayResolution.x, (float)g_Graphic.m_DisplayResolution.y });
            graphicsState.bindings = { bindingSet };
            graphicsState.vertexBuffers = { nvrhi::VertexBufferBinding{ g_CommonResources.UnitSphere.m_VertexBuffer } };
            graphicsState.indexBuffer = nvrhi::IndexBufferBinding{ g_CommonResources.UnitSphere.m_IndexBuffer, Graphic::kIndexBufferFormat };
            graphicsState.indirectParams = probeDrawIndirectArgsBuffer;

            commandList->setGraphicsState(graphicsState);
            commandList->setPushConstants(&passConstants, sizeof(passConstants));
            commandList->drawIndexedIndirect(0);
        }
    }
};
static GIDebugRenderer gs_GIDebugRenderer;
IRenderer* g_GIDebugRenderer = &gs_GIDebugRenderer;
