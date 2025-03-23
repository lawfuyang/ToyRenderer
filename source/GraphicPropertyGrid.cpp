#include "GraphicPropertyGrid.h"

#include "extern/imgui/imgui.h"

#include "Graphic.h"
#include "Engine.h"
#include "Scene.h"

#if 0
namespace nvrhi::d3d12
{
    extern D3D12MA::Allocator* g_D3D12MAAllocator;
}
#endif

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

        ImGui::Checkbox("Enable Pass Culling", &params.m_bPassCulling);
        ImGui::Checkbox("Enable Resource Tracking", &params.m_bResourceAliasing);
        ImGui::Separator();

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debug"))
    {
        DebugControllables& params = m_DebugControllables;

        ImGui::SliderInt("FPS Limit", (int*)&params.m_FPSLimit, 10, 240);

        // keep in sync with 'kDeferredLightingDebugMode_*'
        static const char* kDebugModeNames[] =
        {
            "None",
            "Lighting Only",
            "Colorize Instances",
            "Colorize Meshlets",
            "Albedo",
            "Normal",
            "Emissive",
            "Metalness",
            "Roughness",
            "Ambient Occlusion",
            "Ambient",
            "Shadow Mask",
            "Mesh LOD",
            "Motion Vectors"
        };

        static int debugModeIdx = 0;
        if (ImGui::Combo("##DebugModeCombo", &debugModeIdx, kDebugModeNames, std::size(kDebugModeNames)))
        {
            params.m_DebugMode = debugModeIdx;
        }

        if (ImGui::Checkbox("GPU Stable Power State", &params.m_GPUStablePowerState))
        {
            g_Graphic.SetGPUStablePowerState(params.m_GPUStablePowerState);
        }

        ImGui::Checkbox("Enable Animations", &params.m_EnableAnimations);

        ImGui::TreePop();
    }

	if (ImGui::TreeNode("Instance Rendering"))
	{
		InstanceRenderingControllables& params = m_InstanceRenderingControllables;

		ImGui::Checkbox("Enable Frustum Culling", &params.m_bEnableFrustumCulling);
		ImGui::Checkbox("Enable Occlusion Culling", &params.m_bEnableOcclusionCulling);
        ImGui::Checkbox("Enable Meshlet Cone Culling", &params.m_bEnableMeshletConeCulling);
		ImGui::Checkbox("Freeze Culling Camera", &params.m_bFreezeCullingCamera);
        ImGui::SliderInt("Force Mesh LOD", &params.m_ForceMeshLOD, -1, Graphic::kMaxNumMeshLODs - 1);

		ImGui::TreePop();
	}

    if (ImGui::TreeNode("Lighting"))
    {
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
        ImGui::Checkbox("Enable Soft Shadows", &params.m_bEnableSoftShadows);
        ImGui::Checkbox("Enable Shadow Denoising", &params.m_bEnableShadowDenoising);
        ImGui::SliderFloat("Sun Angular Diameter", &params.m_SunAngularDiameter, 0.0f, 3.0f);

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
