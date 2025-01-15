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
    };
    DebugControllables m_DebugControllables;

    struct InstanceRenderingControllables
    {
        bool m_bEnableFrustumCulling = true;
        bool m_bEnableOcclusionCulling = true;
        bool m_bEnableMeshletRendering = true;
        bool m_bEnableMeshletConeCulling = true;
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

    struct ShadowControllables
    {
        bool m_bEnabled = true;
        float m_MaxShadowDistance = 200.0f;
        float m_CSMSplitLambda = 0.9f;
        const uint32_t m_ShadowMapResolution = 2048; // NOTE: don't allow the shadow map resolution to be changed during runtime. Lazy to handle it.
    };
    ShadowControllables m_ShadowControllables;

    struct AmbientOcclusionControllables
    {
        bool m_bEnabled = true;
    };
    AmbientOcclusionControllables m_AmbientOcclusionControllables;

    struct LightingControllables
    {
        int m_DebugMode = 0;
    };
    LightingControllables m_LightingControllables;
    
    struct BloomControllables
    {
        bool m_bEnabled = true;
        uint32_t m_NbBloomMips = 6;
        float m_UpsampleFilterRadius = 0.005f;
        float m_BloomStrength = 0.1f;
    };
    BloomControllables m_BloomControllables;

    struct RenderGraphControllables
    {
        bool m_bPassCulling = true;
        bool m_bResourceAliasing = true;
    };
    RenderGraphControllables m_RenderGraphControllables;
};
#define g_GraphicPropertyGrid GraphicPropertyGrid::GetInstance()
