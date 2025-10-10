#include "Graphic.h"

#include "extern/imgui/imgui.h"
#include "rtxgi/ddgi/DDGIRootConstants.h"
#include "rtxgi/ddgi/DDGIVolume.h"
#include "shaders/DDGIShaderConfig.h"

#include "CommonResources.h"
#include "Engine.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Utilities.h"

#include "shaders/ShaderInterop.h"

static_assert(RTXGI_DDGI_WAVE_LANE_COUNT == kNumThreadsPerWave);
static_assert(RTXGI_DDGI_BLEND_RAYS_PER_PROBE % kNumThreadsPerWave == 0);

RenderGraph::ResourceHandle g_RTDDRTDDGIVolumeDescsBuffer;
extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

// dont bother with using rtxgi::d3d12::DDGIVolume. We handle all resources internally. Just re-use their logic.
class RTDDGIVolume
    : public rtxgi::DDGIVolumeBase
    , public RTDDGIVolumeBase
{
public:
    nvrhi::TextureHandle GetProbeDataTexture() const override
    {
        return g_Scene->IsDDGIEnabled() ? m_ProbeData : g_CommonResources.BlackTexture2DArray.m_NVRHITextureHandle;
    }

    nvrhi::TextureHandle GetProbeIrradianceTexture() const override
    {
        return g_Scene->IsDDGIEnabled() ? m_ProbeIrradiance : g_CommonResources.BlackTexture2DArray.m_NVRHITextureHandle;
    }

    nvrhi::TextureHandle GetProbeDistanceTexture() const override
    {
        return g_Scene->IsDDGIEnabled() ? m_ProbeDistance : g_CommonResources.BlackTexture2DArray.m_NVRHITextureHandle;
    }

    void Setup(RenderGraph& renderGraph)
    {
        g_Scene->m_RTDDGIVolume = this;

        // just check for these 3 for validity
        if (!m_ProbeIrradiance && !m_ProbeDistance && !m_ProbeData)
        {
            const Vector3 sceneProbeAABBExtents = Vector3{ g_Scene->m_AABB.Extents } *1.1f; // add some padding to the scene AABB

            // enforce minimum of 10x10x10 probes
            m_ProbeSpacing.x = std::min(m_ProbeSpacing.x, sceneProbeAABBExtents.x * 0.2f);
            m_ProbeSpacing.y = std::min(m_ProbeSpacing.y, sceneProbeAABBExtents.y * 0.2f);
            m_ProbeSpacing.z = std::min(m_ProbeSpacing.z, sceneProbeAABBExtents.z * 0.2f);

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

            m_Desc.origin = rtxgi::float3{ g_Scene->m_AABB.Center.x, g_Scene->m_AABB.Center.y, g_Scene->m_AABB.Center.z };
            m_Desc.eulerAngles = rtxgi::float3{ 0.0f, 0.0f, 0.0f }; // TODO: OBB?
            m_Desc.probeSpacing = rtxgi::float3{ m_ProbeSpacing.x, m_ProbeSpacing.y, m_ProbeSpacing.z };
            m_Desc.probeCounts = volumeProbeCounts;
            m_Desc.probeNumRays = RTXGI_DDGI_BLEND_RAYS_PER_PROBE;
            m_Desc.probeNumIrradianceTexels = kNumProbeRadianceTexels;
            m_Desc.probeNumIrradianceInteriorTexels = kNumProbeRadianceTexels - 2;
            m_Desc.probeNumDistanceTexels = kNumProbeDistanceTexels;
            m_Desc.probeNumDistanceInteriorTexels = kNumProbeDistanceTexels - 2;
            m_Desc.probeMaxRayDistance = g_Scene->m_BoundingSphere.Radius; // empirical shit. Just use scene BS radius
            m_Desc.probeRelocationEnabled = true;
            m_Desc.probeRelocationNeedsReset = true;
            m_Desc.probeClassificationEnabled = true;
            m_Desc.probeClassificationNeedsReset = true;
            m_Desc.probeVariabilityEnabled = true;
            m_Desc.probeRayDataFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::RayData];
            m_Desc.probeIrradianceFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Irradiance];
            m_Desc.probeDistanceFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Distance];       // not used in RTXGI Shaders, but init anyway
            m_Desc.probeDataFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Data];               // not used in RTXGI Shaders, but init anyway
            m_Desc.probeVariabilityFormat = kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Variability]; // not used in RTXGI Shaders, but init anyway
            m_Desc.movementType = rtxgi::EDDGIVolumeMovementType::Default;
            m_Desc.probeVisType = rtxgi::EDDGIVolumeProbeVisType::Hide_Inactive;

            if (g_Scene->m_BoundingSphere.Radius < 3.0f)
            {
                // sample's cornell settings:
                m_Desc.probeViewBias = 0.1f;
                m_Desc.probeNormalBias = 0.02f;
                m_Desc.probeMinFrontfaceDistance = 0.1f;
                m_DebugProbeRadius = 0.05f;
            }
            else
            {
                // sample's sponza settings:
                m_Desc.probeViewBias = 0.3f;
                m_Desc.probeNormalBias = 0.1f;
                m_Desc.probeMinFrontfaceDistance = 0.3f;
                m_DebugProbeRadius = 0.1f;
            }

            // sample's cornell & sponza has these values
            m_Desc.probeIrradianceThreshold = 0.2f;
            m_Desc.probeBrightnessThreshold = 0.1f;

            // make radiance delta faster. default: 0.97
            m_Desc.probeHysteresis = 0.50f;

            // leave these values as defaults?
            m_Desc.probeDistanceExponent = 50.f;
            m_Desc.probeIrradianceEncodingGamma = 5.f;
            m_Desc.probeRandomRayBackfaceThreshold = 0.1f;
            m_Desc.probeFixedRayBackfaceThreshold = 0.25f;

            check(GetNumProbes() > 0);

            SDL_Log("Creating GI volume, origin: [%.1f, %.1f, %.1f], num probes: [%u, %u, %u]",
                m_desc.origin.x, m_desc.origin.y, m_desc.origin.z,
                m_desc.probeCounts.x, m_desc.probeCounts.y, m_desc.probeCounts.z);

            m_ProbeIrradiance = CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Irradiance);
            m_ProbeDistance = CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Distance);
            m_ProbeData = CreateProbeTexture(rtxgi::EDDGIVolumeTextureType::Data);

            for (uint32_t i = 0; i < 2; ++i)
            {
                nvrhi::TextureDesc desc;
                desc.format = kProbeTextureFormatsNVRHI[(int)rtxgi::EDDGIVolumeTextureType::VariabilityAverage];
                desc.debugName = "Probe Variability Readback Staging Texture";
                desc.initialState = nvrhi::ResourceStates::CopyDest;

                m_ProbeVariabilityReadbackStagingTextures[i] = g_Graphic.m_NVRHIDevice->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
            }

            // Store the volume rotation
            m_rotationMatrix = EulerAnglesToRotationMatrix(m_desc.eulerAngles);
            m_rotationQuaternion = RotationMatrixToQuaternion(m_rotationMatrix);

            // Set the default scroll anchor to the origin
            m_probeScrollAnchor = m_desc.origin;

            SeedRNG(std::random_device{}());
        }
        
        renderGraph.CreateTransientResource(m_ProbeRayDataRDGTextureHandle, GetProbeTextureDesc(rtxgi::EDDGIVolumeTextureType::RayData));
        renderGraph.CreateTransientResource(m_ProbeVariabilityRDGTextureHandle, GetProbeTextureDesc(rtxgi::EDDGIVolumeTextureType::Variability));
        renderGraph.CreateTransientResource(m_ProbeVariabilityAverageRDGTextureHandle, GetProbeTextureDesc(rtxgi::EDDGIVolumeTextureType::VariabilityAverage));
    }

    void Update() override
    {
        rtxgi::DDGIVolumeBase::Update();

        m_VariabilitiesCursor = (m_VariabilitiesCursor + 1) % kMinimumVariabilitySamples;

        float sum = 0.0f;
        for (float val : m_Variabilities)
        {
            sum += val;
        }
        const float mean = sum / kMinimumVariabilitySamples;

        float variance = 0.0f;
        for (float val : m_Variabilities)
        {
            float diff = val - mean;
            variance += diff * diff;
        }

        m_VariabilityStdDev = std::sqrt(variance / kMinimumVariabilitySamples);
        bIsConverged = GetProbeVariabilityEnabled() && (m_NumVolumeVariabilitySamples++ > kMinimumVariabilitySamples) && (m_VariabilityStdDev < m_VariabilityStdDevThreshold);
    }

    void SetVariabilityForCurrentFrame(float v)
    {
        if (!std::isnormal(v))
        {
            v = 0;
        }
        
        m_Variabilities[m_VariabilitiesCursor] = v;
    }

    void Destroy() override {}

    // direct accessors to private rxtgi::DDGIVolumeBase members, because im lazy
    rtxgi::DDGIVolumeDesc& m_Desc = m_desc;
    float& m_AverageVariability = m_averageVariability;
    
    nvrhi::TextureHandle m_ProbeIrradiance;         // Probe irradiance texture array - RGB irradiance, encoded with a high gamma curve
    nvrhi::TextureHandle m_ProbeDistance;           // Probe distance texture array - R: mean distance | G: mean distance^2
    nvrhi::TextureHandle m_ProbeData;               // Probe data texture array - XYZ: world-space relocation offsets | W: classification state

    RenderGraph::ResourceHandle m_ProbeRayDataRDGTextureHandle;            // Probe ray data texture array - RGB: radiance | A: hit distance
    RenderGraph::ResourceHandle m_ProbeVariabilityRDGTextureHandle;        // Probe variability texture array
    RenderGraph::ResourceHandle m_ProbeVariabilityAverageRDGTextureHandle; // Average of Probe variability for whole volume

    nvrhi::StagingTextureHandle m_ProbeVariabilityReadbackStagingTextures[2]; // CPU-readable resource containing final Probe variability average

    uint32_t m_NumVolumeVariabilitySamples = 0;
    float m_DebugProbeRadius = 0.1f;
    float m_VariabilityStdDev = kKindaBigNumber;
    float m_VariabilityStdDevThreshold = 0.001f;
    bool bIsConverged = false;

    bool m_bResetProbes = true;
    Vector3 m_ProbeSpacing{ 1.0f, 1.0f, 1.0f };

private:
    static const uint32_t kMinimumVariabilitySamples = 16;
    float m_Variabilities[kMinimumVariabilitySamples]{};
    uint32_t m_VariabilitiesCursor = 0;

    inline static const rtxgi::EDDGIVolumeTextureFormat kProbeTextureFormats[(int)rtxgi::EDDGIVolumeTextureType::Count] =
    {
        EDDGIVolumeTextureFormat::F32x2,
        EDDGIVolumeTextureFormat::U32,
        EDDGIVolumeTextureFormat::F16x2, // Note: in large environments FP16 may not be sufficient
        EDDGIVolumeTextureFormat::F16x4,
        EDDGIVolumeTextureFormat::F16,
        EDDGIVolumeTextureFormat::F32x2,
    };

    inline static const nvrhi::Format kProbeTextureFormatsNVRHI[(int)rtxgi::EDDGIVolumeTextureType::Count] =
    {
        nvrhi::Format::RG32_FLOAT,
        nvrhi::Format::R10G10B10A2_UNORM,
        nvrhi::Format::RG16_FLOAT, // Note: in large environments FP16 may not be sufficient
        nvrhi::Format::RGBA16_FLOAT,
        nvrhi::Format::R16_FLOAT,
        nvrhi::Format::RG32_FLOAT,
    };

    struct ProbeTextureCreateInfo
    {
        const char* m_Name;
        nvrhi::ResourceStates m_InitialState;
        bool m_bIsRenderTarget;
    };

    const ProbeTextureCreateInfo kTextureCreateInfos[(int)rtxgi::EDDGIVolumeTextureType::Count] =
    {
        { "Probe Ray Data", nvrhi::ResourceStates::UnorderedAccess, false },
        { "Probe Irradiance", nvrhi::ResourceStates::ShaderResource, true },
        { "Probe Distance", nvrhi::ResourceStates::ShaderResource, true },
        { "Probe Data", nvrhi::ResourceStates::UnorderedAccess, false },
        { "Probe Variability", nvrhi::ResourceStates::UnorderedAccess, false },
        { "Probe Variability Average", nvrhi::ResourceStates::UnorderedAccess, false },
    };

    nvrhi::TextureDesc GetProbeTextureDesc(rtxgi::EDDGIVolumeTextureType textureType)
    {
        uint32_t width, height, arraySize;
        GetDDGIVolumeTextureDimensions(m_desc, textureType, width, height, arraySize);
        check(!(width <= 0 || height <= 0 || arraySize <= 0));

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

        //SDL_Log("DDGI volume texture: %s, %ux%u, %u slices, format: %s", desc.debugName.c_str(), desc.width, desc.height, desc.arraySize, nvrhi::utils::FormatToString(desc.format));

        return desc;
    }

    nvrhi::TextureHandle CreateProbeTexture(rtxgi::EDDGIVolumeTextureType textureType)
    {
        return g_Graphic.m_NVRHIDevice->createTexture(GetProbeTextureDesc(textureType));
    }
};

class GIRenderer : public IRenderer
{
public:
    RTDDGIVolume m_RTDDGIVolume;

    GIRenderer() : IRenderer("GIRenderer") {}

    bool HasImguiControls() const override { return true; }

    void UpdateImgui() override
    {
        ImGui::Checkbox("Enabled", &g_Scene->m_bEnableGI);

        ImGui::Combo("GI Mode", (int*)&g_Scene->m_GITechnique, "DDGI\0RTXGI\0");

        switch (g_Scene->m_GITechnique)
        {
        case GITechnique::DDGI:
        {
            if (!g_Scene->IsDDGIEnabled())
            {
                break;
            }

            rtxgi::DDGIVolumeDesc& volumeDesc = m_RTDDGIVolume.m_Desc;

            ImGui::Checkbox("Show Debug Probes", &volumeDesc.showProbes);

            if (volumeDesc.showProbes)
            {
                ImGui::Indent();
                ImGui::Checkbox("Hide Inactive Probes", (bool*)&volumeDesc.probeVisType);
                ImGui::DragFloat("Probe Radius", &m_RTDDGIVolume.m_DebugProbeRadius, 0.01f, 0.05f, 0.2f, "%.2f");
                ImGui::Unindent();
            }

            //m_RTDDGIVolume.m_bResetProbes = ImGui::Button("Reset Probes");
            volumeDesc.probeRelocationNeedsReset |= ImGui::Checkbox("Enable Probe Relocation", &volumeDesc.probeRelocationEnabled);
            volumeDesc.probeClassificationNeedsReset |= ImGui::Checkbox("Enable Probe Classification", &volumeDesc.probeClassificationEnabled);
            ImGui::Checkbox("Enable Probe Variability", &volumeDesc.probeVariabilityEnabled);
            ImGui::DragFloat("Probe Variability Std Dev Threshold", &m_RTDDGIVolume.m_VariabilityStdDevThreshold, 0.001f, 0.001f, 0.1f, "%.3f");
            ImGui::Text("Probe Spacing: [%.1f, %.1f, %.1f]", m_RTDDGIVolume.m_ProbeSpacing.x, m_RTDDGIVolume.m_ProbeSpacing.y, m_RTDDGIVolume.m_ProbeSpacing.z); // TODO: run-time probe spacing change
            ImGui::Text("Volume Variability Average: [%.3f]", m_RTDDGIVolume.GetVolumeAverageVariability());
            ImGui::Text("Probe Variability Std Dev: [%.3f]", m_RTDDGIVolume.m_VariabilityStdDev);

            break;
        }

        case GITechnique::ReSTIR:
        {
            break;
        }
        };
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        ON_EXIT_SCOPE_LAMBDA([this]()
            {
                if (!g_Scene->IsDDGIEnabled())
                {
                    g_Scene->m_RTDDGIVolume = nullptr;
                    m_RTDDGIVolume.m_ProbeIrradiance = nullptr;
                    m_RTDDGIVolume.m_ProbeDistance = nullptr;
                    m_RTDDGIVolume.m_ProbeData = nullptr;
                    m_RTDDGIVolume.m_bResetProbes = true;
                }
            });

        if (!g_Scene->IsGIEnabled())
        {
            return false;
        }

        switch (g_Scene->m_GITechnique)
        {
        case GITechnique::DDGI:
        {
            if (!g_Scene->IsDDGIEnabled())
            {
                return false;
            }

            m_RTDDGIVolume.Setup(renderGraph);

            {
                nvrhi::BufferDesc desc;
                desc.byteSize = sizeof(rtxgi::DDGIVolumeDescGPUPacked) + 1; // TODO: multiple volumes
                desc.structStride = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
                desc.debugName = "DDGI Volume Desc GPU Packed";
                desc.initialState = nvrhi::ResourceStates::ShaderResource;
                renderGraph.CreateTransientResource(g_RTDDRTDDGIVolumeDescsBuffer, desc);
            }

            break;
        }

        case GITechnique::ReSTIR:
        {
            break;
        }
        }

        return true;
    }

    void TraceProbes(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        PROFILE_GPU_SCOPED(commandList, __FUNCTION__);

        nvrhi::TextureHandle probeRayDataTexture = renderGraph.GetTexture(m_RTDDGIVolume.m_ProbeRayDataRDGTextureHandle);
        nvrhi::BufferHandle RTDDGIVolumeDescsBuffer = renderGraph.GetBuffer(g_RTDDRTDDGIVolumeDescsBuffer);

        GIProbeTraceConsts passConstants;
        passConstants.m_DirectionalLightVector = g_Scene->m_DirLightVec;
        passConstants.m_DirectionalLightStrength = g_Scene->m_DirLightStrength;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(passConstants)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, RTDDGIVolumeDescsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(1, m_RTDDGIVolume.m_ProbeData),
            nvrhi::BindingSetItem::Texture_SRV(2, m_RTDDGIVolume.m_ProbeIrradiance),
            nvrhi::BindingSetItem::Texture_SRV(3, m_RTDDGIVolume.m_ProbeDistance),
            nvrhi::BindingSetItem::RayTracingAccelStruct(4, g_Scene->m_TLAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, g_Scene->m_InstanceConstsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, g_Graphic.m_GlobalVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, g_Graphic.m_GlobalMaterialDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(8, g_Graphic.m_GlobalIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(9, g_Graphic.m_GlobalMeshDataBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0, probeRayDataTexture),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.AnisotropicClampSampler),
            nvrhi::BindingSetItem::Sampler(1, g_CommonResources.AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(2, g_CommonResources.LinearWrapSampler),
        };

        uint32_t dispatchX, dispatchY, dispatchZ;
        m_RTDDGIVolume.GetRayDispatchDimensions(dispatchX, dispatchY, dispatchZ);

        // DXC complains: "Function uses derivatives in compute-model shader with NumThreads (1, 1, 1); derivatives require NumThreads to be 1D and a multiple of 4, or 2D/3D with X and Y both being a multiple of 2."
        // we're not doing any hardware derivatives, so silence that error, enforce dispatchX to be multiple of kNumThreadsPerWave
        check(dispatchX % kNumThreadsPerWave == 0);

        Graphic::ComputePassParams computePassParams;
        computePassParams.m_CommandList = commandList;
        computePassParams.m_ShaderName = "giprobetrace_CS_ProbeTrace";
        computePassParams.m_BindingSetDesc = bindingSetDesc;
        computePassParams.m_ExtraBindingSets = { g_Graphic.GetSrvUavCbvDescriptorTable() };
        computePassParams.m_ExtraBindingLayouts = { g_Graphic.m_SrvUavCbvBindlessLayout };
        computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(Vector3U{ dispatchX, dispatchY, dispatchZ }, Vector3U{ kNumThreadsPerWave, 1, 1 });
        computePassParams.m_PushConstantsData = &passConstants;
        computePassParams.m_PushConstantsBytes = sizeof(passConstants);
        g_Graphic.AddComputePass(computePassParams);
    }

    void RenderDDGI(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        check(g_Scene->IsDDGIEnabled());
        
        if (m_RTDDGIVolume.m_bResetProbes)
        {
            m_RTDDGIVolume.m_NumVolumeVariabilitySamples = 0;
            commandList->clearTextureFloat(m_RTDDGIVolume.m_ProbeIrradiance, nvrhi::AllSubresources, m_RTDDGIVolume.m_ProbeIrradiance->getDesc().clearValue);
            commandList->clearTextureFloat(m_RTDDGIVolume.m_ProbeDistance, nvrhi::AllSubresources, m_RTDDGIVolume.m_ProbeDistance->getDesc().clearValue);
            m_RTDDGIVolume.m_bResetProbes = false;
        }

        rtxgi::DDGIVolumeDesc& volumeDesc = m_RTDDGIVolume.m_Desc;

        m_RTDDGIVolume.Update();

        if (m_RTDDGIVolume.bIsConverged)
        {
            // TODO: run a "cheap" trace & blend pass, but without relocation & classification, to get the average variability once converged
            return;
        }

        nvrhi::TextureHandle probeRayDataTexture = renderGraph.GetTexture(m_RTDDGIVolume.m_ProbeRayDataRDGTextureHandle);
        nvrhi::TextureHandle probeVariabilityTexture = renderGraph.GetTexture(m_RTDDGIVolume.m_ProbeVariabilityRDGTextureHandle);
        nvrhi::TextureHandle probeVariabilityAverageTexture = renderGraph.GetTexture(m_RTDDGIVolume.m_ProbeVariabilityAverageRDGTextureHandle);
        nvrhi::BufferHandle RTDDGIVolumeDescsBuffer = renderGraph.GetBuffer(g_RTDDRTDDGIVolumeDescsBuffer);

        const rtxgi::DDGIVolumeDescGPUPacked volumeDescGPU = m_RTDDGIVolume.GetDescGPUPacked();
        commandList->writeBuffer(RTDDGIVolumeDescsBuffer, &volumeDescGPU, sizeof(rtxgi::DDGIVolumeDescGPUPacked));

        TraceProbes(commandList, renderGraph);

        // TODO: multiple volumes
        DDGIRootConstants rootConsts{ volumeDesc.index, 0, 0 };

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::PushConstants(kDDGIRootConstsRegister, sizeof(DDGIRootConstants)),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, RTDDGIVolumeDescsBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0, probeRayDataTexture),
            nvrhi::BindingSetItem::Texture_UAV(1, m_RTDDGIVolume.m_ProbeIrradiance),
            nvrhi::BindingSetItem::Texture_UAV(2, m_RTDDGIVolume.m_ProbeDistance),
            nvrhi::BindingSetItem::Texture_UAV(3, m_RTDDGIVolume.m_ProbeData),
            nvrhi::BindingSetItem::Texture_UAV(4, probeVariabilityTexture),
            nvrhi::BindingSetItem::Texture_UAV(5, probeVariabilityAverageTexture),
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

        const Vector3U relocationAndClassificationGroupSize = ComputeShaderUtils::GetGroupCount(m_RTDDGIVolume.GetNumProbes(), 32);

        if (m_RTDDGIVolume.GetProbeRelocationEnabled())
        {
            computePassParams.m_ShaderName = "ProbeRelocationCS_DDGIProbeRelocationCS";
            computePassParams.m_DispatchGroupSize = relocationAndClassificationGroupSize;
            g_Graphic.AddComputePass(computePassParams);
        }

        if (m_RTDDGIVolume.GetProbeClassificationEnabled())
        {
            computePassParams.m_ShaderName = "ProbeClassificationCS_DDGIProbeClassificationCS";
            computePassParams.m_DispatchGroupSize = relocationAndClassificationGroupSize;
            g_Graphic.AddComputePass(computePassParams);
        }

        if (m_RTDDGIVolume.GetProbeVariabilityEnabled())
        {
            nvrhi::StagingTextureHandle thisFrameVariabilityTexture = m_RTDDGIVolume.m_ProbeVariabilityReadbackStagingTextures[g_Graphic.m_FrameCounter % 2];

            size_t outRowPitch;
            const float* variabilityReadback = (const float*)g_Graphic.m_NVRHIDevice->mapStagingTexture(thisFrameVariabilityTexture, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Read, &outRowPitch);
            check(variabilityReadback);

            m_RTDDGIVolume.m_AverageVariability = *variabilityReadback;
            m_RTDDGIVolume.SetVariabilityForCurrentFrame(*variabilityReadback);

            g_Graphic.m_NVRHIDevice->unmapStagingTexture(thisFrameVariabilityTexture);

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

                computePassParams.m_ShaderName = bIsFirstPass ? "ReductionCS_DDGIReductionCS REDUCTION=1" : "ReductionCS_DDGIExtraReductionCS";
                computePassParams.m_DispatchGroupSize = Vector3U{ outputTexelsX, outputTexelsY, outputTexelsZ };
                g_Graphic.AddComputePass(computePassParams);

                // Each thread group will write out a value to the averaging texture
                // If there is more than one thread group, we will need to do extra averaging passes
                inputTexelsX = outputTexelsX;
                inputTexelsY = outputTexelsY;
                inputTexelsZ = outputTexelsZ;

                bIsFirstPass = false;
            }

            commandList->copyTexture(thisFrameVariabilityTexture, nvrhi::TextureSlice{}, probeVariabilityAverageTexture, nvrhi::TextureSlice{ 0,0,0,1,1,1 });
        }
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        switch (g_Scene->m_GITechnique)
        {
        case GITechnique::DDGI:
        {
            RenderDDGI(commandList, renderGraph);
            break;
        }

        case GITechnique::ReSTIR:
        {
            break;
        }
        };
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

    bool SetupDDGI(RenderGraph& renderGraph)
    {
        if (!g_Scene->IsDDGIEnabled())
        {
            return false;
        }

        const rtxgi::DDGIVolumeDesc& volumeDesc = gs_GIRenderer.m_RTDDGIVolume.m_Desc;
        if (!volumeDesc.showProbes)
        {
            return false;
        }

        renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);

        {
            nvrhi::BufferDesc desc;
            desc.byteSize = sizeof(Vector3) * gs_GIRenderer.m_RTDDGIVolume.GetNumProbes();
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
            desc.byteSize = sizeof(uint32_t) * gs_GIRenderer.m_RTDDGIVolume.GetNumProbes();
            desc.structStride = sizeof(uint32_t);
            desc.canHaveUAVs = true;
            desc.debugName = "Instance ID to Probe Index";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            renderGraph.CreateTransientResource(m_InstanceIDToProbeIndexRDGBufferHandle, desc);
        }

        renderGraph.AddReadDependency(g_RTDDRTDDGIVolumeDescsBuffer);

        return true;
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        switch (g_Scene->m_GITechnique)
        {
        case GITechnique::DDGI:
            return SetupDDGI(renderGraph);

        case GITechnique::ReSTIR:
            break;
        }

        return false;
    }

    void RenderDDGIDebug(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph)
    {
        check(g_Scene->IsDDGIEnabled());

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::BufferHandle probePositionsBuffer = renderGraph.GetBuffer(m_ProbePositionsRDGBufferHandle);
        nvrhi::BufferHandle probeDrawIndirectArgsBuffer = renderGraph.GetBuffer(m_ProbeDrawIndirectArgsRDGBufferHandle);
        nvrhi::BufferHandle instanceIDToProbeIndexBuffer = renderGraph.GetBuffer(m_InstanceIDToProbeIndexRDGBufferHandle);
        nvrhi::BufferHandle RTDDGIVolumeDescsBuffer = renderGraph.GetBuffer(g_RTDDRTDDGIVolumeDescsBuffer);

        DrawIndexedIndirectArguments indirectArgs{};
        indirectArgs.m_IndexCount = g_CommonResources.UnitSphere.m_NumIndices;
        commandList->writeBuffer(probeDrawIndirectArgsBuffer, &indirectArgs, sizeof(indirectArgs));

        const uint32_t numProbes = gs_GIRenderer.m_RTDDGIVolume.GetNumProbes();

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
            passParameters.m_ProbeRadius = gs_GIRenderer.m_RTDDGIVolume.m_DebugProbeRadius;
            passParameters.m_bHideInactiveProbes = gs_GIRenderer.m_RTDDGIVolume.GetProbeVisType() == rtxgi::EDDGIVolumeProbeVisType::Hide_Inactive;

            nvrhi::BufferHandle passParametersBuffer = g_Graphic.CreateConstantBuffer(commandList, passParameters);

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::ConstantBuffer(0, passParametersBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, g_Scene->m_HZB),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(10, RTDDGIVolumeDescsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, probePositionsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(1, probeDrawIndirectArgsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(2, instanceIDToProbeIndexBuffer),
                nvrhi::BindingSetItem::Texture_UAV(10, gs_GIRenderer.m_RTDDGIVolume.m_ProbeData),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampMinReductionSampler),
            };

            Graphic::ComputePassParams computePassParams;
            computePassParams.m_CommandList = commandList;
            computePassParams.m_ShaderName = "giprobevisualization_CS_VisualizeGIProbesCulling";
            computePassParams.m_BindingSetDesc = bindingSetDesc;
            computePassParams.m_DispatchGroupSize = ComputeShaderUtils::GetGroupCount(numProbes, kNumThreadsPerWave);

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
            passParameters.m_ProbeRadius = gs_GIRenderer.m_RTDDGIVolume.m_DebugProbeRadius;

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings =
            {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(GIProbeVisualizationConsts)),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, probePositionsBuffer),
                nvrhi::BindingSetItem::Texture_SRV(1, gs_GIRenderer.m_RTDDGIVolume.m_ProbeData),
                nvrhi::BindingSetItem::Texture_SRV(2, gs_GIRenderer.m_RTDDGIVolume.m_ProbeIrradiance),
                nvrhi::BindingSetItem::Texture_SRV(3, gs_GIRenderer.m_RTDDGIVolume.m_ProbeDistance),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(4, RTDDGIVolumeDescsBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(5, instanceIDToProbeIndexBuffer),
                nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearWrapSampler),
            };

            nvrhi::BindingSetHandle bindingSet;
            nvrhi::BindingLayoutHandle bindingLayout;
            g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

            nvrhi::BlendState blendState;
            blendState.targets[0] = g_CommonResources.BlendOpaque;

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = g_CommonResources.m_UncompressedRawVertexFormatInputLayoutHandle;
            pipelineDesc.VS = g_Graphic.GetShader("giprobevisualization_VS_VisualizeGIProbes");
            pipelineDesc.PS = g_Graphic.GetShader("giprobevisualization_PS_VisualizeGIProbes");
            pipelineDesc.renderState = nvrhi::RenderState{ blendState, g_CommonResources.DepthWriteStencilNone, g_CommonResources.CullBackFace };
            pipelineDesc.bindingLayouts = { bindingLayout };

            nvrhi::GraphicsState graphicsState;
            graphicsState.pipeline = g_Graphic.GetOrCreatePSO(pipelineDesc, frameBuffer);
            graphicsState.framebuffer = frameBuffer;
            graphicsState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)g_Graphic.m_RenderResolution.x, (float)g_Graphic.m_RenderResolution.y });
            graphicsState.bindings = { bindingSet };
            graphicsState.vertexBuffers = { nvrhi::VertexBufferBinding{ g_CommonResources.UnitSphere.m_VertexBuffer } };
            graphicsState.indexBuffer = nvrhi::IndexBufferBinding{ g_CommonResources.UnitSphere.m_IndexBuffer, GraphicConstants::kIndexBufferFormat };
            graphicsState.indirectParams = probeDrawIndirectArgsBuffer;

            commandList->setGraphicsState(graphicsState);
            commandList->setPushConstants(&passParameters, sizeof(passParameters));
            commandList->drawIndexedIndirect(0);
        }
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        switch (g_Scene->m_GITechnique)
        {
        case GITechnique::DDGI:
            return RenderDDGIDebug(commandList, renderGraph);

        case GITechnique::ReSTIR:
            break;
        }
    }
};
static GIDebugRenderer gs_GIDebugRenderer;
IRenderer* g_GIDebugRenderer = &gs_GIDebugRenderer;
