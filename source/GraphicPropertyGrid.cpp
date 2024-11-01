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
        ImGui::Checkbox("Render Debug Draw Demo", &params.m_bRenderDebugDrawDemo);
        ImGui::TreePop();
    }

	if (ImGui::TreeNode("GBuffer"))
	{
		InstanceRenderingControllables& params = m_InstanceRenderingControllables;

		ImGui::Checkbox("Enable Frustum Culling", &params.m_bEnableFrustumCulling);
		ImGui::Checkbox("Enable Occlusion Culling", &params.m_bEnableOcclusionCulling);
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

        ImGui::Checkbox("Enabled", &params.m_bEnabled);
        ImGui::Separator();

        extern IRenderer* g_AmbientOcclusionRenderer;
        g_AmbientOcclusionRenderer->UpdateImgui();

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
