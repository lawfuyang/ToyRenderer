#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "rtxgi/ddgi/DDGIVolume.h"
#include "shaders/DDGIShaderConfig.h"

#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Utilities.h"

#include "shaders/ShaderInterop.h"

static_assert(RTXGI_DDGI_WAVE_LANE_COUNT == kNumThreadsPerWave);

RenderGraph::ResourceHandle g_DDGIOutputRDGTextureHandle;

// dont bother with using rtxgi::d3d12::DDGIVolume. We handle all resources internally. Just re-use their logic.
class GIVolume : public rtxgi::DDGIVolumeBase
{
public:
    void Create(const rtxgi::DDGIVolumeDesc& desc)
    {
        m_desc = desc;
        
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
    GIVolume m_GIVolume;

    bool m_bShowProbes = false;
    bool m_bResetProbes = true;
    Vector3 m_ProbeSpacing{ 1.0f, 1.0f, 1.0f };
    uint32_t m_ProbeNumRays = 256;

public:
    GIRenderer() : IRenderer("GIRenderer") {}

    void PostSceneLoad() override
    {
        static const int kProbeNumIrradianceTexels = 8;
        static const int kProbeNumDistanceTexels = 16;

        Scene* scene = g_Graphic.m_Scene.get();
        auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        // enforce minimum of 10x10x10 probes
        m_ProbeSpacing.x = std::min(m_ProbeSpacing.x, scene->m_AABB.Extents.x * 0.1f);
        m_ProbeSpacing.y = std::min(m_ProbeSpacing.y, scene->m_AABB.Extents.z * 0.1f);
        m_ProbeSpacing.z = std::min(m_ProbeSpacing.z, scene->m_AABB.Extents.y * 0.1f);

        // XY = horizontal plane, Z = vertical plane
        const rtxgi::int3 volumeProbeCounts =
        {
            (int)std::ceil(scene->m_AABB.Extents.x / m_ProbeSpacing.x),
            (int)std::ceil(scene->m_AABB.Extents.z / m_ProbeSpacing.y),
            (int)std::ceil(scene->m_AABB.Extents.y / m_ProbeSpacing.z)
        };

        rtxgi::DDGIVolumeDesc volumeDesc;
        volumeDesc.origin = rtxgi::float3{ scene->m_AABB.Center.x, scene->m_AABB.Center.y, scene->m_AABB.Center.z };
        volumeDesc.eulerAngles = rtxgi::float3{ 0.0f, 0.0f, 0.0f }; // TODO: OBB?
        volumeDesc.probeSpacing = rtxgi::float3{ m_ProbeSpacing.x, m_ProbeSpacing.y, m_ProbeSpacing.z };
        volumeDesc.probeCounts = volumeProbeCounts;
        volumeDesc.probeNumRays = m_ProbeNumRays;
        volumeDesc.probeNumIrradianceTexels = kProbeNumIrradianceTexels;
        volumeDesc.probeNumIrradianceInteriorTexels = kProbeNumIrradianceTexels - 2;
        volumeDesc.probeNumDistanceTexels = kProbeNumDistanceTexels;
        volumeDesc.probeNumDistanceInteriorTexels = kProbeNumDistanceTexels - 2;
        volumeDesc.probeMaxRayDistance = scene->m_BoundingSphere.Radius; // empirical shit. Just use scene BS radius
        volumeDesc.probeRelocationEnabled = true;
        volumeDesc.probeClassificationEnabled = true;
        volumeDesc.probeVariabilityEnabled = true;
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

        m_GIVolume.Create(volumeDesc);
    }

    void UpdateImgui() override
    {
        auto& controllables = g_GraphicPropertyGrid.m_GIControllables;

        ImGui::Checkbox("Enabled", &controllables.m_bEnabled);

        if (!controllables.m_bEnabled)
        {
            return;
        }

        ImGui::Checkbox("Show Probes", &m_bShowProbes);
        ImGui::Checkbox("Reset Probes", &m_bResetProbes);
        ImGui::InputFloat3("Probe Spacing", (float*)&m_ProbeSpacing, "%.2f");
        ImGui::DragInt("Probe Num Rays", (int*)&m_ProbeNumRays, 1.0f, 128, 512);
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

    void ShowProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {

    }

    void ResetProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        m_GIVolume.ClearProbes(commandList);
        //DDGIProbeRelocationResetCS
        //DDGIProbeClassificationResetCS
    }

    void ReadbackDDGIVolumeVariability(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void RayTraceVolumes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void UpdateDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void RelocateDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void ClassifyDDGIVolumeProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void CalculateDDGIVolumeVariability(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}
    void GatherIndirectLighting(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) {}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::TextureHandle ddgiOutputTexture = renderGraph.GetTexture(g_DDGIOutputRDGTextureHandle);

        if (m_bResetProbes)
        {
            ResetProbes(commandList, renderGraph);
            m_bResetProbes = false;
        }

        m_GIVolume.Update();

        if (m_bShowProbes)
        {
            ShowProbes(commandList, renderGraph);
        }

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
