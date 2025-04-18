#include "GraphicPropertyGrid.h"

#include "extern/imgui/imgui.h"

#include "Graphic.h"
#include "Engine.h"
#include "Scene.h"
#include "RenderGraph.h"

void GraphicPropertyGrid::UpdateIMGUI()
{
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
        g_Scene->m_RenderGraph->UpdateIMGUI();

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

    if (ImGui::TreeNode("GI"))
    {
        extern IRenderer* g_GIRenderer;
        g_GIRenderer->UpdateImgui();

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

        ImGui::Text("Scene Luminance: %f", g_Scene->m_LastFrameExposure);
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
}
