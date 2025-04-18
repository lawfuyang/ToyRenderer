#pragma once

#include "MathUtilities.h"

class GraphicPropertyGrid
{
public:
    SingletonFunctionsMeyers(GraphicPropertyGrid);

	void UpdateIMGUI();

    struct DebugControllables
    {
        uint32_t m_FPSLimit = 200.0f;
        int m_DebugMode = 0;
        bool m_GPUStablePowerState = false;
        bool m_EnableAnimations = true;
    };
    DebugControllables m_DebugControllables;

    struct InstanceRenderingControllables
    {
        bool m_bEnableFrustumCulling = true;
        bool m_bEnableOcclusionCulling = true;
        bool m_bEnableMeshletConeCulling = true;
        bool m_bFreezeCullingCamera = false;
        int m_ForceMeshLOD = -1;
    };
	InstanceRenderingControllables m_InstanceRenderingControllables;

    struct SkyControllables
    {
        bool m_bEnabled = true;
        float m_SkyTurbidity = 2.0f;
        Vector3 m_GroundAlbedo{ 0.1f, 0.1f, 0.1f };
    };
    SkyControllables m_SkyControllables;

    struct AdaptLuminanceControllables
    {
        float m_ManualExposureOverride = 0.0f; // 0 = automatic
        float m_MinimumLuminance = 0.004f;
        float m_MaximumLuminance = 12.0f;
        float m_AutoExposureSpeed = 0.0025f;
        float m_MiddleGray = 0.18f;
    };
    AdaptLuminanceControllables m_AdaptLuminanceControllables;
};
#define g_GraphicPropertyGrid GraphicPropertyGrid::GetInstance()
