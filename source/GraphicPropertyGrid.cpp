#include "GraphicPropertyGrid.h"

#include "extern/imgui/imgui.h"
#include "extern/d3d12ma/D3D12MemAlloc.h"

#include "Graphic.h"
#include "Engine.h"
#include "Scene.h"

namespace nvrhi::d3d12
{
    extern D3D12MA::Allocator* g_D3D12MAAllocator;
}

void UpdateIMGUIGraphicPropertyGrid()
{
    g_GraphicPropertyGrid.UpdateIMGUI();
}

void GraphicPropertyGrid::UpdateIMGUI()
{
    Scene* scene = g_Graphic.m_Scene.get();

    if (ImGui::TreeNode("Shaders"))
    {
        if (ImGui::Button("Compile & Reload Shaders"))
        {
            std::system(StringFormat("\"%s/../compileallshaders\" NO_PAUSE", GetExecutableDirectory()));
            g_Graphic.m_bTriggerReloadShaders = true;
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Render Graph"))
    {
        auto& params = m_RenderGraphControllables;

        // when this IMGUI tree node is open, always enable the 'm_bUpdateIMGUI' flag
        params.m_bUpdateIMGUI = true;

        ImGui::Checkbox("Enable Pass Culling", &params.m_bPassCulling);
        ImGui::Checkbox("Enable Resource Tracking", &params.m_bResourceAliasing);
        ImGui::Separator();

        scene->m_RenderGraph->DrawIMGUI();

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("D3D12MA Stats"))
    {
        auto* allocator = nvrhi::d3d12::g_D3D12MAAllocator;
        assert(allocator);

        D3D12MA::TotalStatistics statistics;
        allocator->CalculateStatistics(&statistics);

        D3D12MA::Budget budget;
        allocator->GetBudget(&budget, nullptr);

        const float usageBytes = BYTES_TO_MB(budget.UsageBytes);
        const float budgetBytes = BYTES_TO_MB(budget.BudgetBytes);

        ImGui::Text("Usage: [%f MB], Budget: [%f MB]", usageBytes, budgetBytes);

        for (size_t i = 0; i < 3; ++i)
        {
            switch (i)
            {
            case 0: ImGui::Text("	Default Heap"); break;
            case 1: ImGui::Text("	Upload Heap"); break;
            case 2: ImGui::Text("	ReadBack Heap"); break;
            };
            D3D12MA::DetailedStatistics& detail_stats = statistics.HeapType[i];
            D3D12MA::Statistics& stats = statistics.HeapType[i].Stats;

            const float allocationBytes = BYTES_TO_MB(stats.AllocationBytes);
            const float blockBytes = BYTES_TO_MB(stats.BlockBytes);

            ImGui::Text("		Num Allocations(%u), Allocated(%f MB), Num Heaps(%u), Heap Allocated(%f MB)",
                stats.AllocationCount, allocationBytes, stats.BlockCount, blockBytes);
            ImGui::Text("		Unused Range Count(%u), Allocation Size Min(%llu), Allocation Size Max(%llu), Unused Range Size Min(%llu), Unused Range Size Max(%llu)",
                detail_stats.UnusedRangeCount, detail_stats.AllocationSizeMin, detail_stats.AllocationSizeMax, detail_stats.UnusedRangeSizeMin, detail_stats.UnusedRangeSizeMax);
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debug"))
    {
        DebugControllables& params = m_DebugControllables;

        ImGui::SliderInt("FPS Limit", (int*)&params.m_FPSLimit, 0, 240);
        ImGui::Checkbox("Render Debug Draw", &params.m_bRenderDebugDraw);
        ImGui::Checkbox("Render Grid", &params.m_bRenderGrid);
        ImGui::Checkbox("Draw Scene AABB", &params.m_bRenderSceneAABB);
        ImGui::Checkbox("Draw Scene Bounding Sphere", &params.m_bRenderSceneBS);
        ImGui::Checkbox("Enable GPU Frustum Culling", &params.m_bEnableGPUFrustumCulling);
        ImGui::Checkbox("Enable GPU Occlusion Culling", &params.m_bEnableGPUOcclusionCulling);
        ImGui::Checkbox("Render Debug Draw Demo", &params.m_bRenderDebugDrawDemo);
        ImGui::Checkbox("Colorize Instances", &params.m_bColorizeInstances);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Lighting"))
    {
        LightingControllables& params = m_LightingControllables;
        ImGui::Checkbox("Cull Far Depth Tiles", &params.m_bCullFarDepthTiles);
        ImGui::Checkbox("Tile Rendering Use CS", &params.m_bTileRenderingUseCS);
        ImGui::Checkbox("Enable Deferred Lighting Tile Classification Debug", &params.m_bEnableDeferredLightingTileClassificationDebug);
        ImGui::Checkbox("Lighting Only Debug", &params.m_bLightingOnlyDebug);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ambient Occlusion"))
    {
        AmbientOcclusionControllables& params = m_AmbientOcclusionControllables;

        // copied from XeGTAO.h because they retardly inlined imgui code in the header file & wrapped it around "IMGUI_API"
        static auto GTAOImGuiSettings = [](XeGTAO::GTAOSettings& settings)
            {
                ImGui::PushItemWidth(120.0f);

                ImGui::Text("Performance/quality settings:");

                ImGui::Combo("Quality Level", &settings.QualityLevel, "Low\0Medium\0High\0Ultra\00");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher quality settings use more samples per pixel but are slower");
                settings.QualityLevel = std::clamp(settings.QualityLevel, 0, 3);

                ImGui::Combo("Denoising level", &settings.DenoisePasses, "Disabled\0Sharp\0Medium\0Soft\00");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The amount of edge-aware spatial denoise applied");
                settings.DenoisePasses = std::clamp(settings.DenoisePasses, 0, 3);

                ImGui::Text("Visual settings:");

                settings.Radius = std::clamp(settings.Radius, 0.0f, 100000.0f);

                ImGui::InputFloat("Effect radius", &settings.Radius, 0.05f, 0.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("World (viewspace) effect radius\nExpected range: depends on the scene & requirements, anything from 0.01 to 1000+");
                settings.Radius = std::clamp(settings.Radius, 0.0f, 10000.0f);

                if (ImGui::CollapsingHeader("Auto-tuned settings (heuristics)"))
                {
                    ImGui::InputFloat("Radius multiplier", &settings.RadiusMultiplier, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the 'Effect Radius' - used by the auto-tune to best match raytraced ground truth\nExpected range: [0.3, 3.0], defaults to %.3f", XE_GTAO_DEFAULT_RADIUS_MULTIPLIER);
                    settings.RadiusMultiplier = std::clamp(settings.RadiusMultiplier, 0.3f, 3.0f);

                    ImGui::InputFloat("Falloff range", &settings.FalloffRange, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gently reduce sample impact as it gets out of 'Effect radius' bounds\nExpected range: [0.0, 1.0], defaults to %.3f", XE_GTAO_DEFAULT_FALLOFF_RANGE);
                    settings.FalloffRange = std::clamp(settings.FalloffRange, 0.0f, 1.0f);

                    ImGui::InputFloat("Sample distribution power", &settings.SampleDistributionPower, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make samples on a slice equally distributed (1.0) or focus more towards the center (>1.0)\nExpected range: [1.0, 3.0], 2defaults to %.3f", XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER);
                    settings.SampleDistributionPower = std::clamp(settings.SampleDistributionPower, 1.0f, 3.0f);

                    ImGui::InputFloat("Thin occluder compensation", &settings.ThinOccluderCompensation, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slightly reduce impact of samples further back to counter the bias from depth-based (incomplete) input scene geometry data\nExpected range: [0.0, 0.7], defaults to %.3f", XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION);
                    settings.ThinOccluderCompensation = std::clamp(settings.ThinOccluderCompensation, 0.0f, 0.7f);

                    ImGui::InputFloat("Final power", &settings.FinalValuePower, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Applies power function to the final value: occlusion = pow( occlusion, finalPower )\nExpected range: [0.5, 5.0], defaults to %.3f", XE_GTAO_DEFAULT_FINAL_VALUE_POWER);
                    settings.FinalValuePower = std::clamp(settings.FinalValuePower, 0.5f, 5.0f);

                    ImGui::InputFloat("Depth MIP sampling offset", &settings.DepthMIPSamplingOffset, 0.05f, 0.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mainly performance (texture memory bandwidth) setting but as a side-effect reduces overshadowing by thin objects and increases temporal instability\nExpected range: [2.0, 6.0], defaults to %.3f", XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET);
                    settings.DepthMIPSamplingOffset = std::clamp(settings.DepthMIPSamplingOffset, 0.0f, 30.0f);
                }

                ImGui::PopItemWidth();
            };

        ImGui::Checkbox("Enabled", &params.m_bEnabled);
        ImGui::Separator();
        ImGui::Combo("Debug Output Mode", &params.m_DebugOutputMode, "None\0Screen-Space Normals\0Edges\0Bent Normals\0");
        ImGui::Separator();
        GTAOImGuiSettings(params.m_XeGTAOSettings);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Shadows"))
    {
        ShadowControllables& params = m_ShadowControllables;

        ImGui::Checkbox("Enabled", &params.m_bEnabled);

        bool bCalculateCSMSplitDistances = false;
        bCalculateCSMSplitDistances |= ImGui::DragFloat("Max Shadow Distance", &params.m_MaxShadowDistance, 1.0f, 1.0f);
        bCalculateCSMSplitDistances |= ImGui::SliderFloat("CSM Split Lambda", &params.m_CSMSplitLambda, 0.01f, 1.0f);
        if (bCalculateCSMSplitDistances)
        {
            scene->CalculateCSMSplitDistances();
        }

        params.m_bShadowMapResolutionDirty = false;

        int shadowMapRes = (int)params.m_ShadowMapResolution;
        if (ImGui::InputInt("Shadow Map Resolution", &shadowMapRes, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            shadowMapRes = std::clamp(GetNextPow2(shadowMapRes), 128u, 4096u);
            params.m_ShadowMapResolution = shadowMapRes;

			params.m_bShadowMapResolutionDirty = true;
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Sky"))
    {
        SkyControllables& params = m_SkyControllables;

        ImGui::SliderFloat3("Ground Albedo", (float*)&params.m_GroundAlbedo, 0.0f, 1.0f);
        ImGui::SliderFloat("Sky Turbidity", &params.m_SkyTurbidity, 1.0f, 10.0f);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("HDR"))
    {
        AdaptLuminanceControllables& params = m_AdaptLuminanceControllables;

        bool bLuminanceDirty = false;

        ImGui::Text("Scene Luminance: %f", scene->m_LastFrameExposure);
        ImGui::DragFloat("Manual Exposure Override", &params.m_ManualExposureOverride, 0.1f, 0.0f);
        bLuminanceDirty |= ImGui::DragFloat("Minimum Luminance", &params.m_MinimumLuminance, 0.01f, 0.0f);
        bLuminanceDirty |= ImGui::DragFloat("Maximum Luminance", &params.m_MaximumLuminance, 0.01f, 0.0f);
        ImGui::DragFloat("Auto Exposure Speed", &params.m_AutoExposureSpeed, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Middle Gray", &params.m_MiddleGray, 0.01f, 0.0f);
        ImGui::DragFloat("White Point", &params.m_WhitePoint, 0.01f, 0.0f);

        if (bLuminanceDirty)
        {
            params.m_MaximumLuminance = std::max(params.m_MaximumLuminance, params.m_MinimumLuminance + 0.1f);
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bloom"))
    {
        BloomControllables& params = m_BloomControllables;

        const uint32_t nbMaxBloomMips = ComputeNbMips(g_Graphic.m_RenderResolution.x, g_Graphic.m_RenderResolution.y);

        ImGui::Checkbox("Enabled", &params.m_bEnabled);
        ImGui::SliderInt("Number of Bloom Mips", (int*)&params.m_NbBloomMips, 2, nbMaxBloomMips);
        ImGui::SliderFloat("Upsample Filter Radius", &params.m_UpsampleFilterRadius, 0.001f, 0.1f);
        ImGui::SliderFloat("Bloom Strength", &params.m_BloomStrength, 0.01f, 1.0f);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Scene"))
    {
        g_Graphic.m_Scene->UpdateIMGUIPropertyGrid();

        ImGui::TreePop();
    }
}
