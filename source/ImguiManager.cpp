#include "Imguimanager.h"

#include "extern/imgui/backends/imgui_impl_win32.h"

#include "Engine.h"
#include "Utilities.h"

void IMGUIManager::Initialize()
{
    PROFILE_FUNCTION();

    ImGui::CreateContext();
    verify(ImGui_ImplWin32_Init(g_Engine.m_WindowHandle));
}

void IMGUIManager::ShutDown()
{
    PROFILE_FUNCTION();

    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void IMGUIManager::ProcessWindowsMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

void IMGUIManager::Update()
{
    PROFILE_FUNCTION();

    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    // Show all IMGUI widget demos
    if (m_bShowDemoWindows)
    {
        ImGui::ShowDemoWindow();
    }

    if (m_bShowGraphicPropertyGrid)
    {
        if (ImGui::Begin("Graphic Property Grid", &m_bShowGraphicPropertyGrid, ImGuiWindowFlags_AlwaysAutoResize))
        {
            extern void UpdateIMGUIGraphicPropertyGrid();
            UpdateIMGUIGraphicPropertyGrid();
        }
        ImGui::End();
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Menu"))
        {
            if (ImGui::MenuItem("Show Graphic Property Grid"))
            {
                m_bShowGraphicPropertyGrid = !m_bShowGraphicPropertyGrid;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Toggle IMGUI Demo Windows"))
            {
                m_bShowDemoWindows = !m_bShowDemoWindows;
            }

            ImGui::EndMenu();
        }

        ImGui::Text("\tCPU: [%.2f ms]", g_Engine.m_CPUFrameTimeMs);
        ImGui::SameLine();
        ImGui::Text("\tGPU: [%.2f] ms", g_Engine.m_GPUTimeMs);
        ImGui::Text("\tFPS: [%.1f]", 1000.0f / std::max((float)g_Engine.m_CPUFrameTimeMs, g_Engine.m_GPUTimeMs));

        extern CommandLineOption<bool> g_EnableD3DDebug;
        if (g_EnableD3DDebug.Get())
        {
            ImGui::SameLine();
            ImGui::Text("\tD3D12 DEBUG LAYER ENABLED!");
        }

        ImGui::EndMainMenuBar();
    }
}
