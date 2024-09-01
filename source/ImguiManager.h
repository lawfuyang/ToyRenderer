#pragma once

#include "extern/imgui/imgui.h"
#include "extern/imgui/imgui_stdlib.h"

#include "MathUtilities.h"

class IMGUIManager
{
public:
    struct IMGUICmdList
    {
        std::vector<ImDrawVert> m_VB;
        std::vector<ImDrawIdx> m_IB;
        std::vector<ImDrawCmd> m_DrawCmd;
    };

    struct IMGUIDrawData
    {
        std::vector<IMGUICmdList> m_DrawList;
        uint32_t m_VtxCount = 0;
        uint32_t m_IdxCount = 0;
        Vector2 m_Pos = Vector2::Zero;
        Vector2 m_Size = Vector2::Zero;
    };

    void Initialize();
    void ShutDown();
    void ProcessWindowsMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Update();
    void SaveDrawData();

    void UpdateMainMenuBar();

    IMGUIDrawData m_PendingDrawData;

    bool m_bInitDone = false;
    bool m_bShowDemoWindows = false;
    bool m_bShowNodeEditor = false;
    bool m_bShowGraphicPropertyGrid = false;
    bool m_bOpenWorld = false;

    friend class Engine;
    friend class IMGUIRenderer;
};
