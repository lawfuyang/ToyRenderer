#pragma once

#include "extern/imgui/imgui.h"
#include "extern/imgui/imgui_stdlib.h"

#include "MathUtilities.h"

class IMGUIManager
{
public:
    void Initialize();
    void ShutDown();
    void ProcessWindowsMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Update();

    void UpdateMainMenuBar();

    bool m_bInitDone = false;
    bool m_bShowDemoWindows = false;
    bool m_bShowNodeEditor = false;
    bool m_bShowGraphicPropertyGrid = false;

    friend class Engine;
    friend class IMGUIRenderer;
};
