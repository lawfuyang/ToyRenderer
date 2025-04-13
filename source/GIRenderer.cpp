#include "Graphic.h"

#include "extern/imgui/imgui.h"
#include "rtxgi/ddgi/DDGIRootConstants.h"
#include "rtxgi/ddgi/DDGIVolume.h"
#include "shaders/DDGIShaderConfig.h"

#include "CommonResources.h"
#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Utilities.h"

#include "shaders/ShaderInterop.h"

CommandLineOption<bool> g_CVarUseDDGICornellDDGIVolumeSettings{ "UseDDGICornellDDGIVolumeSettings", false };
CommandLineOption<bool> g_CVarUseDDGISponzaDDGIVolumeSettings{ "UseDDGISponzaDDGIVolumeSettings", false };

static_assert(RTXGI_DDGI_WAVE_LANE_COUNT == kNumThreadsPerWave);
static_assert(RTXGI_DDGI_BLEND_RAYS_PER_PROBE % kNumThreadsPerWave == 0);

extern RenderGraph::ResourceHandle g_GBufferARDGTextureHandle;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

const rtxgi::EDDGIVolumeTextureFormat kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Count] =
{
    EDDGIVolumeTextureFormat::F32x2,
    EDDGIVolumeTextureFormat::U32,
    EDDGIVolumeTextureFormat::F16x2, // Note: in large environments FP16 may not be sufficient
    EDDGIVolumeTextureFormat::F16x4,
    EDDGIVolumeTextureFormat::F16,
    EDDGIVolumeTextureFormat::F32x2,
};

static const nvrhi::Format kProbeTextureFormatsNVRHI[(int)rtxgi::EDDGIVolumeTextureType::Count] =
{
    nvrhi::Format::RG32_FLOAT,
    nvrhi::Format::R10G10B10A2_UNORM,
    nvrhi::Format::RG16_FLOAT, // Note: in large environments FP16 may not be sufficient
    nvrhi::Format::RGBA16_FLOAT,
    nvrhi::Format::R16_FLOAT,
    nvrhi::Format::RG32_FLOAT,
};

// dont bother with using rtxgi::d3d12::DDGIVolume. We handle all resources internally. Just re-use their logic.
class GIVolume
    : public rtxgi::DDGIVolumeBase
    , public GIVolumeBase
{
public:
    nvrhi::TextureHandle GetProbeDataTexture() const override
    {
        return m_ProbeData;
    }

    nvrhi::TextureHandle GetProbeIrradianceTexture() const override
    {
        return g_GraphicPropertyGrid.m_GIControllables.m_bEnabled ? m_ProbeIrradiance : g_CommonResources.BlackTexture2DArray.m_NVRHITextureHandle;
    }

    nvrhi::TextureHandle GetProbeDistanceTexture() const override
    {
        return g_GraphicPropertyGrid.m_GIControllables.m_bEnabled ? m_ProbeDistance : g_CommonResources.BlackTexture2DArray.m_NVRHITextureHandle;
    }

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

        m_ProbeVariabilityReadback.Initialize(kProbeTextureFormatsNVRHI[(int)rtxgi::EDDGIVolumeTextureType::VariabilityAverage]);

        // Store the volume rotation
        m_rotationMatrix = EulerAnglesToRotationMatrix(m_desc.eulerAngles);
        m_rotationQuaternion = RotationMatrixToQuaternion(m_rotationMatrix);

        // Set the default scroll anchor to the origin
        m_probeScrollAnchor = m_desc.origin;

        SeedRNG(std::random_device{}());

        m_averageVariability = kKindaBigNumber;
    }

    void Destroy() override {}

    rtxgi::DDGIVolumeDesc& GetDesc() { return m_desc; }
    uint32_t GetNumProbes() const { return rtxgi::DDGIVolumeBase::GetNumProbes(); }
    void SetVariability(float v) { m_averageVariability = v; }

    nvrhi::TextureHandle m_ProbeRayData;            // Probe ray data texture array - RGB: radiance | A: hit distance
    nvrhi::TextureHandle m_ProbeIrradiance;         // Probe irradiance texture array - RGB irradiance, encoded with a high gamma curve
    nvrhi::TextureHandle m_ProbeDistance;           // Probe distance texture array - R: mean distance | G: mean distance^2
    nvrhi::TextureHandle m_ProbeData;               // Probe data texture array - XYZ: world-space relocation offsets | W: classification state
    nvrhi::TextureHandle m_ProbeVariability;        // Probe variability texture array
    nvrhi::TextureHandle m_ProbeVariabilityAverage; // Average of Probe variability for whole volume
    FencedReadbackTexture m_ProbeVariabilityReadback; // CPU-readable resource containing final Probe variability average

    uint32_t m_NumVolumeVariabilitySamples = 0;
    float m_DebugProbeRadius = 0.1f;
    float m_ProbeVariabilityThreshold = 0.4f;

private:
    struct ProbeTextureCreateInfo
    {
        nvrhi::TextureHandle& m_TextureHandle;
        const char* m_Name;
        nvrhi::ResourceStates m_InitialState;
        bool m_bIsRenderTarget;
    };

    const ProbeTextureCreateInfo kTextureCreateInfos[(int)rtxgi::EDDGIVolumeTextureType::Count] =
    {
        { m_ProbeRayData, "Probe Ray Data", nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeIrradiance, "Probe Irradiance", nvrhi::ResourceStates::ShaderResource, true },
        { m_ProbeDistance, "Probe Distance", nvrhi::ResourceStates::ShaderResource, true },
        { m_ProbeData, "Probe Data", nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeVariability, "Probe Variability", nvrhi::ResourceStates::UnorderedAccess, false },
        { m_ProbeVariabilityAverage, "Probe Variability Average", nvrhi::ResourceStates::UnorderedAccess, false },
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
        desc.format = kProbeTextureFormatsNVRHI[(int)textureType];
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

    GIVolume m_GIVolume;

    GIRenderer() : IRenderer("GIRenderer") {}

    void Initialize() override
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = sizeof(rtxgi::DDGIVolumeDescGPUPacked); // TODO: multiple volumes
        desc.structStride = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
        desc.debugName = "DDGI Volume Desc GPU Packed";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;

        g_Scene->m_GIVolumeDescsBuffer = g_Graphic.m_NVRHIDevice->createBuffer(desc);
    }

    void PostSceneLoad() override
    {
        g_Scene->m_GIVolume = &m_GIVolume;

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
        volumeDesc.probeNumRays = RTXGI_DDGI_BLEND_RAYS_PER_PROBE;
        volumeDesc.probeNumIrradianceTexels = kNumProbeRadianceTexels;
        volumeDesc.probeNumIrradianceInteriorTexels = kNumProbeRadianceTexels - 2;
        volumeDesc.probeNumDistanceTexels = kNumProbeDistanceTexels;
        volumeDesc.probeNumDistanceInteriorTexels = kNumProbeDistanceTexels - 2;
        volumeDesc.probeMaxRayDistance = g_Scene->m_BoundingSphere.Radius; // empirical shit. Just use scene BS radius
        volumeDesc.probeRelocationEnabled = true;
        volumeDesc.probeRelocationNeedsReset = true;
        volumeDesc.probeClassificationEnabled = true;
        volumeDesc.probeClassificationNeedsReset = true;
        volumeDesc.probeVariabilityEnabled = true;
        volumeDesc.probeRayDataFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::RayData];
        volumeDesc.probeIrradianceFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Irradiance];
        volumeDesc.probeDistanceFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Distance];       // not used in RTXGI Shaders, but init anyway
        volumeDesc.probeDataFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Data];               // not used in RTXGI Shaders, but init anyway
        volumeDesc.probeVariabilityFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Variability]; // not used in RTXGI Shaders, but init anyway
        volumeDesc.movementType = rtxgi::EDDGIVolumeMovementType::Default;
        volumeDesc.probeVisType = rtxgi::EDDGIVolumeProbeVisType::Hide_Inactive;
        
        if (g_Scene->m_bIsSmallScene)
        {
            // sample's cornell settings:
            volumeDesc.probeViewBias = 0.1f;
            volumeDesc.probeNormalBias = 0.02f;
            volumeDesc.probeMinFrontfaceDistance = 0.1f;
            m_GIVolume.m_ProbeVariabilityThreshold = 0.1f;
            m_GIVolume.m_DebugProbeRadius = 0.05f;
        }
        else
        {
            // sample's sponza settings:
            volumeDesc.probeViewBias = 0.3f;
            volumeDesc.probeNormalBias = 0.1f;
            volumeDesc.probeMinFrontfaceDistance = 0.3f;
            m_GIVolume.m_ProbeVariabilityThreshold = 0.4f;
            m_GIVolume.m_DebugProbeRadius = 0.1f;
        }

        // sample's cornell & sponza has these values
        volumeDesc.probeIrradianceThreshold = 0.2f;
        volumeDesc.probeBrightnessThreshold = 0.1f;

        // leave these values as defaults?
        volumeDesc.probeHysteresis = 0.97f;
        volumeDesc.probeDistanceExponent = 50.f;
        volumeDesc.probeIrradianceEncodingGamma = 5.f;
        volumeDesc.probeRandomRayBackfaceThreshold = 0.1f;
        volumeDesc.probeFixedRayBackfaceThreshold = 0.25f;

        if (g_CVarUseDDGICornellDDGIVolumeSettings.Get())
        {
            volumeDesc.origin = rtxgi::float3{ 0.0f, 1.0f, 0.0f };
            volumeDesc.probeCounts = rtxgi::int3{ 9, 9, 9 };
            volumeDesc.probeSpacing = rtxgi::float3{ 0.3f, 0.3f, 0.3f };
            volumeDesc.probeMaxRayDistance = 10.0f;
        }
        else if (g_CVarUseDDGISponzaDDGIVolumeSettings.Get())
        {
            volumeDesc.origin = rtxgi::float3{ -0.4f, 5.4f, -0.25f };
            volumeDesc.probeCounts = rtxgi::int3{ 22, 22, 22 };
            volumeDesc.probeSpacing = rtxgi::float3{ 1.02f, 0.5f, 0.45f };
            volumeDesc.probeMaxRayDistance = 10000.0f;
        }

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

        rtxgi::DDGIVolumeDesc& volumeDesc = m_GIVolume.GetDesc();

        ImGui::Checkbox("Show Debug Probes", &volumeDesc.showProbes);

        if (volumeDesc.showProbes)
        {
            ImGui::Indent();
            ImGui::Checkbox("Hide Inactive Probes", (bool*)&volumeDesc.probeVisType);
            ImGui::DragFloat("Probe Radius", &m_GIVolume.m_DebugProbeRadius, 0.01f, 0.05f, 0.2f, "%.2f");
            ImGui::Unindent();
        }

        m_bResetProbes = ImGui::Button("Reset Probes");
        volumeDesc.probeRelocationNeedsReset = ImGui::Button("Reset Relocation");
        volumeDesc.probeClassificationNeedsReset = ImGui::Button("Reset Classification");
        volumeDesc.probeRelocationNeedsReset |= ImGui::Checkbox("Enable Probe Relocation", &volumeDesc.probeRelocationEnabled);
        volumeDesc.probeClassificationNeedsReset |= ImGui::Checkbox("Enable Probe Classification", &volumeDesc.probeClassificationEnabled);
        ImGui::DragFloat3("Probe Spacing", (float*)&m_ProbeSpacing, 1.0f, 0.1f, 2.0f);
        ImGui::Text("Volume Variability Average: %f", m_GIVolume.GetVolumeAverageVariability());
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        const auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        if (!controllables.m_bEnabled)
        {
            return false;
        }

        if (!g_Scene->m_GIVolume)
        {
            return false;
        }

        return true;
    }

    void ResetProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        if (m_bResetProbes)
        {
            m_GIVolume.m_NumVolumeVariabilitySamples = 0;
            commandList->clearTextureFloat(m_GIVolume.m_ProbeIrradiance, nvrhi::AllSubresources, m_GIVolume.m_ProbeIrradiance->getDesc().clearValue);
            commandList->clearTextureFloat(m_GIVolume.m_ProbeDistance, nvrhi::AllSubresources, m_GIVolume.m_ProbeDistance->getDesc().clearValue);
        }

        rtxgi::DDGIVolumeDesc& volumeDesc = m_GIVolume.GetDesc();

        // TODO: multiple volumes
        DDGIRootConstants rootConsts{ volumeDesc.index, 0, 0 };

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(kDDGIRootConstsRegister, sizeof(rootConsts)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(kDDGIVolumeDescGPUPackedRegister, g_Scene->m_GIVolumeDescsBuffer),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIProbeDataRegister, m_GIVolume.m_ProbeRayData),
        };

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(m_GIVolume.GetNumProbes(), 32);
        computePassParams.m_PushConstantsData = &rootConsts;
        computePassParams.m_PushConstantsBytes = sizeof(rootConsts);

        if (volumeDesc.probeRelocationEnabled && volumeDesc.probeRelocationNeedsReset)
        {
            computePassParams.m_ShaderName = "ProbeRelocationCS_DDGIProbeRelocationResetCS";
            g_Graphic.AddComputePass(computePassParams);
        }

        if (volumeDesc.probeClassificationEnabled && volumeDesc.probeClassificationNeedsReset)
        {
            computePassParams.m_ShaderName = "ProbeClassificationCS_DDGIProbeClassificationResetCS";
            g_Graphic.AddComputePass(computePassParams);
        }

        m_bResetProbes = volumeDesc.probeRelocationNeedsReset = volumeDesc.probeClassificationNeedsReset = false;
    }

    void TraceProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);

        GIProbeTraceConsts passConstants;
        passConstants.m_DirectionalLightVector = g_Scene->m_DirLightVec;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, g_Scene->m_GIVolumeDescsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(1, m_GIVolume.m_ProbeData),
            nvrhi::BindingSetItem::Texture_SRV(2, m_GIVolume.m_ProbeIrradiance),
            nvrhi::BindingSetItem::Texture_SRV(3, m_GIVolume.m_ProbeDistance),
            nvrhi::BindingSetItem::RayTracingAccelStruct(4, g_Scene->m_TLAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, g_Graphic.m_GlobalMaterialDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(8, g_Graphic.m_GlobalIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(9, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0, m_GIVolume.m_ProbeRayData),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicClamp, g_CommonResources.AnisotropicClampSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicWrap, g_CommonResources.AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicBorder, g_CommonResources.AnisotropicBorderSampler),
            nvrhi::BindingSetItem::Sampler(SamplerIdx_AnisotropicMirror, g_CommonResources.AnisotropicMirrorSampler),
            nvrhi::BindingSetItem::Sampler(4, g_CommonResources.LinearWrapSampler),
        };

        uint32_t dispatchX, dispatchY, dispatchZ;
        m_GIVolume.GetRayDispatchDimensions(dispatchX, dispatchY, dispatchZ);

        // DXC complains: "Function uses derivatives in compute-model shader with NumThreads (1, 1, 1); derivatives require NumThreads to be 1D and a multiple of 4, or 2D/3D with X and Y both being a multiple of 2."
        // we're not doing any hardware derivatives, so silence that error, enforce dispatchX to be multiple of kNumThreadsPerWave
        assert(dispatchX % kNumThreadsPerWave == 0);

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "giprobetrace_CS_ProbeTrace";
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector3U{ dispatchX, dispatchY, dispatchZ }, Vector3U{ kNumThreadsPerWave, 1, 1 });
        computePassParams.m_PushConstantsData = &passConstants;
        computePassParams.m_PushConstantsBytes = sizeof(passConstants);
        computePassParams.m_ShouldAddBindlessResources = true;
        g_Graphic.AddComputePass(computePassParams);
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        ResetProbes(commandList, renderGraph);

        m_GIVolume.Update();

        rtxgi::DDGIVolumeDesc& volumeDesc = m_GIVolume.GetDesc();

        const uint32_t kMinimumVariabilitySamples = 16;
        const bool bIsConverged = m_GIVolume.GetProbeVariabilityEnabled()
            && (m_GIVolume.m_NumVolumeVariabilitySamples++ > kMinimumVariabilitySamples)
            && (m_GIVolume.GetVolumeAverageVariability() < m_GIVolume.m_ProbeVariabilityThreshold);

        if (bIsConverged)
        {
            // TODO: run a "cheap" trace & blend pass, but without relocation & classification, to get the average variability once converged
            //return;
        }

        const rtxgi::DDGIVolumeDescGPUPacked volumeDescGPU = m_GIVolume.GetDescGPUPacked();
        commandList->writeBuffer(g_Scene->m_GIVolumeDescsBuffer, &volumeDescGPU, sizeof(rtxgi::DDGIVolumeDescGPUPacked));

        TraceProbes(commandList, renderGraph);

        // TODO: multiple volumes
        DDGIRootConstants rootConsts{ volumeDesc.index, 0, 0 };

        // Resource Descriptor Table
        // 1 SRV for constants structured buffer      (t0, space0)
        // 1 UAV for ray data texture array           (u0, space0)
        // 1 UAV for probe irradiance texture array   (u1, space0)
        // 1 UAV for probe distance texture array     (u2, space0)
        // 1 UAV for probe data texture array         (u3, space0)
        // 1 UAV for probe variation array            (u4, space0)
        // 1 UAV for probe variation average array    (u5, space0)
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(kDDGIRootConstsRegister, sizeof(rootConsts)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(kDDGIVolumeDescGPUPackedRegister, g_Scene->m_GIVolumeDescsBuffer),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIRayDataRegister, m_GIVolume.m_ProbeRayData),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIBlendRadianceOutputRegister, m_GIVolume.m_ProbeIrradiance),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIBlendDistanceOutputRegister, m_GIVolume.m_ProbeDistance),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIProbeDataRegister, m_GIVolume.m_ProbeData),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIProbeVariabilityRegister, m_GIVolume.m_ProbeVariability),
            nvrhi::BindingSetItem::Texture_UAV(kDDGIProbeVariabilityAverageRegister, m_GIVolume.m_ProbeVariabilityAverage),
        };

        UINT probeCountX, probeCountY, probeCountZ;
        rtxgi::GetDDGIVolumeProbeCounts(volumeDesc, probeCountX, probeCountY, probeCountZ);

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "ProbeBlendingCS_DDGIProbeBlendingCS RTXGI_DDGI_BLEND_RADIANCE=1";
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_DispatchGroupSize = Vector3U{ probeCountX, probeCountY, probeCountZ };
        computePassParams.m_PushConstantsData = &rootConsts;
        computePassParams.m_PushConstantsBytes = sizeof(rootConsts);
        g_Graphic.AddComputePass(computePassParams);

        computePassParams.m_ShaderName = "ProbeBlendingCS_DDGIProbeBlendingCS RTXGI_DDGI_BLEND_RADIANCE=0";
        g_Graphic.AddComputePass(computePassParams);

        const Vector3U relocationAndClassificationGroupSize = ComputeShaderUtils::GetGroupCount(m_GIVolume.GetNumProbes(), 32);

        if (m_GIVolume.GetProbeRelocationEnabled())
        {
            computePassParams.m_ShaderName = "ProbeRelocationCS_DDGIProbeRelocationCS";
            computePassParams.m_DispatchGroupSize = relocationAndClassificationGroupSize;
            g_Graphic.AddComputePass(computePassParams);
        }

        if (m_GIVolume.GetProbeClassificationEnabled())
        {
            computePassParams.m_ShaderName = "ProbeClassificationCS_DDGIProbeClassificationCS";
            computePassParams.m_DispatchGroupSize = relocationAndClassificationGroupSize;
            g_Graphic.AddComputePass(computePassParams);
        }

        if (m_GIVolume.GetProbeVariabilityEnabled())
        {
            Vector2 variabilityReadback;
            m_GIVolume.m_ProbeVariabilityReadback.Read(&variabilityReadback);
            m_GIVolume.SetVariability(variabilityReadback.x);

            const Vector3U kNumThreadsInGroup = { 4, 8, 4 }; // Each thread group will have 8x8x8 threads
            const Vector2U kThreadSampleFootprint = { 4, 2 }; // Each thread will sample 4x2 texels

            // Initially, the reduction input is the full variability size (same as irradiance texture without border texels)
            uint32_t inputTexelsX = probeCountX * volumeDesc.probeNumIrradianceInteriorTexels;
            uint32_t inputTexelsY = probeCountY * volumeDesc.probeNumIrradianceInteriorTexels;
            uint32_t inputTexelsZ = probeCountZ;

            bool bIsFirstPass = true;
            while (inputTexelsX > 1 || inputTexelsY > 1 || inputTexelsZ > 1)
            {
                // One thread group per output texel
                const uint32_t outputTexelsX = (uint32_t)ceil((float)inputTexelsX / (kNumThreadsInGroup.x * kThreadSampleFootprint.x));
                const uint32_t outputTexelsY = (uint32_t)ceil((float)inputTexelsY / (kNumThreadsInGroup.y * kThreadSampleFootprint.y));
                const uint32_t outputTexelsZ = (uint32_t)ceil((float)inputTexelsZ / kNumThreadsInGroup.z);

                rootConsts.reductionInputSizeX = inputTexelsX;
                rootConsts.reductionInputSizeY = inputTexelsY;
                rootConsts.reductionInputSizeZ = inputTexelsZ;

                computePassParams.m_ShaderName = bIsFirstPass ? "ReductionCS_DDGIReductionCS" : "ReductionCS_DDGIExtraReductionCS";
                computePassParams.m_DispatchGroupSize = Vector3U{ outputTexelsX, outputTexelsY, outputTexelsZ };
                g_Graphic.AddComputePass(computePassParams);

                // Each thread group will write out a value to the averaging texture
                // If there is more than one thread group, we will need to do extra averaging passes
                inputTexelsX = outputTexelsX;
                inputTexelsY = outputTexelsY;
                inputTexelsZ = outputTexelsZ;

                // Extra reduction passes average values in variability texture down to single value
                bIsFirstPass = false;
            }

            m_GIVolume.m_ProbeVariabilityReadback.CopyTo(commandList, m_GIVolume.m_ProbeVariabilityAverage);
        }
    }
};
static GIRenderer gs_GIRenderer;
IRenderer* g_GIRenderer = &gs_GIRenderer;

class GIDebugRenderer : public IRenderer
{
    RenderGraph::ResourceHandle m_ProbePositionsRDGBufferHandle;
    RenderGraph::ResourceHandle m_ProbeDrawIndirectArgsRDGBufferHandle;
    RenderGraph::ResourceHandle m_InstanceIDToProbeIndexRDGBufferHandle;

public:
    GIDebugRenderer() : IRenderer("GIDebugRenderer") {}

    bool Setup(RenderGraph& renderGraph) override
    {
        const rtxgi::DDGIVolumeDesc& volumeDesc = gs_GIRenderer.m_GIVolume.GetDesc();
        if (!volumeDesc.showProbes)
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

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(uint32_t) * gs_GIRenderer.m_GIVolume.GetNumProbes();
            desc.structStride = sizeof(uint32_t);
            desc.canHaveUAVs = true;
            desc.debugName = "Instance ID to Probe Index";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(m_InstanceIDToProbeIndexRDGBufferHandle, desc);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::BufferHandle probePositionsBuffer = renderGraph.GetBuffer(m_ProbePositionsRDGBufferHandle);
        nvrhi::BufferHandle probeDrawIndirectArgsBuffer = renderGraph.GetBuffer(m_ProbeDrawIndirectArgsRDGBufferHandle);
        nvrhi::BufferHandle instanceIDToProbeIndexBuffer = renderGraph.GetBuffer(m_InstanceIDToProbeIndexRDGBufferHandle);

        DrawIndexedIndirectArguments indirectArgs{};
        indirectArgs.m_IndexCount = g_CommonResources.UnitSphere.m_NumIndices;
        commandList->writeBuffer(probeDrawIndirectArgsBuffer, &indirectArgs, sizeof(indirectArgs));

        const uint32_t numProbes = gs_GIRenderer.m_GIVolume.GetNumProbes();

        // get probe positions from the volume
        {
            Matrix projectionT = g_Scene->m_View.m_ViewToClip.Transpose();
            Vector4 frustumX = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[0] };
            Vector4 frustumY = Vector4{ projectionT.m[3] } + Vector4{ projectionT.m[1] };
            frustumX.Normalize();
            frustumY.Normalize();

            GIProbeVisualizationUpdateConsts passParameters;
			passParameters.m_NumProbes = numProbes;
			passParameters.m_CameraOrigin = g_Scene->m_View.m_Eye;
            passParameters.m_Frustum = Vector4{ frustumX.x, frustumX.z, frustumY.y, frustumY.z };
			passParameters.m_WorldToView = g_Scene->m_View.m_WorldToView;
			passParameters.m_HZBDimensions = Vector2U{ g_Scene->m_HZB->getDesc().width, g_Scene->m_HZB->getDesc().height };
            passParameters.m_P00 = g_Scene->m_View.m_ViewToClip.m[0][0];
            passParameters.m_P11 = g_Scene->m_View.m_ViewToClip.m[1][1];
            passParameters.m_NearPlane = g_Scene->m_View.m_ZNearP;
			passParameters.m_ProbeRadius = gs_GIRenderer.m_GIVolume.m_DebugProbeRadius;
            passParameters.m_bHideInactiveProbes = gs_GIRenderer.m_GIVolume.GetProbeVisType() == rtxgi::EDDGIVolumeProbeVisType::Hide_Inactive;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                nvrhi::BindingSetItem::Texture_SRV(0, g_Scene->m_HZB),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(10, g_Scene->m_GIVolumeDescsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, probePositionsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(1, probeDrawIndirectArgsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(2, instanceIDToProbeIndexBuffer),
                nvrhi::BindingSetItem::Texture_UAV(10, gs_GIRenderer.m_GIVolume.m_ProbeData),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampMinReductionSampler),
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "giprobevisualization_CS_VisualizeGIProbesCulling";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(numProbes, kNumThreadsPerWave);
            computePassParams.m_PushConstantsData = &passParameters;
            computePassParams.m_PushConstantsBytes = sizeof(passParameters);

            g_Graphic.AddComputePass(computePassParams);
        }

        // draw probes
        {
            PROFILE_GPU_SCOPED(commandList, "Draw Probes");

            nvrhi::TextureHandle depthBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);

            nvrhi::FramebufferDesc frameBufferDesc;
            frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());
            frameBufferDesc.setDepthAttachment(depthBuffer);
            nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

            const Matrix matrix = Matrix::CreateFromQuaternion(g_Scene->m_View.m_Orientation);
            const Vector3 forwardVector = matrix.Forward();

            GIProbeVisualizationConsts passParameters;
            passParameters.m_WorldToClip = g_Scene->m_View.m_WorldToClip;
            passParameters.m_CameraDirection = forwardVector;
            passParameters.m_ProbeRadius = gs_GIRenderer.m_GIVolume.m_DebugProbeRadius;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, probePositionsBuffer),
                nvrhi::BindingSetItem::Texture_SRV(1, gs_GIRenderer.m_GIVolume.m_ProbeData),
                nvrhi::BindingSetItem::Texture_SRV(2, gs_GIRenderer.m_GIVolume.m_ProbeIrradiance),
                nvrhi::BindingSetItem::Texture_SRV(3, gs_GIRenderer.m_GIVolume.m_ProbeDistance),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(4, g_Scene->m_GIVolumeDescsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(5, instanceIDToProbeIndexBuffer),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearWrapSampler),
            };

            nvrhi::BindingSetHandle bindingSet;
            nvrhi::BindingLayoutHandle bindingLayout;
            g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = g_CommonResources.m_UncompressedRawVertexFormatInputLayoutHandle;
            pipelineDesc.VS = g_Graphic.GetShader("giprobevisualization_VS_VisualizeGIProbes");
            pipelineDesc.PS = g_Graphic.GetShader("giprobevisualization_PS_VisualizeGIProbes");
            pipelineDesc.renderState = nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthWriteStencilNone, g_CommonResources.CullBackFace };
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
            commandList->setPushConstants(&passParameters, sizeof(passParameters));
            commandList->drawIndexedIndirect(0);
        }
    }
};
static GIDebugRenderer gs_GIDebugRenderer;
IRenderer* g_GIDebugRenderer = &gs_GIDebugRenderer;
