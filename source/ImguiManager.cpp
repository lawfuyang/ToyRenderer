#include "Imguimanager.h"

#include "extern/imgui/imgui_impl_win32.h"

#include "Engine.h"
#include "Utilities.h"

::HANDLE g_IMGUIContextCreatedEvent;

struct IMGUICreateContextEventCreator
{
    IMGUICreateContextEventCreator()
    {
        assert(!g_IMGUIContextCreatedEvent);

        const bool kManualReset = true;
        const bool kInitialState = false;
        g_IMGUIContextCreatedEvent = ::CreateEvent(nullptr, kManualReset, kInitialState, nullptr);
        assert(g_IMGUIContextCreatedEvent);
    }
};
static IMGUICreateContextEventCreator g_IMGUICreateContextEventCreator;

void IMGUIManager::Initialize()
{
    PROFILE_FUNCTION();

    ImGui::CreateContext();
    ::SetEvent(g_IMGUIContextCreatedEvent);

    ::HWND windowHandle = g_Engine.m_WindowHandle;
    while (windowHandle == 0)
    {
        //g_Log.info("Engine window handle not ready for IMGUI yet... sleeping for 1 ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        windowHandle = g_Engine.m_WindowHandle;
    }

    if (!ImGui_ImplWin32_Init(windowHandle))
    {
        assert(0);
    }

    m_bInitDone = true;
}

void IMGUIManager::ShutDown()
{
    PROFILE_FUNCTION();

    ::CloseHandle(g_IMGUIContextCreatedEvent);

    ImGui_ImplWin32_Shutdown();
}

void IMGUIManager::ProcessWindowsMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // this function may be called during init phase...
    if (!m_bInitDone)
        return;

    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

void IMGUIManager::UpdateMainMenuBar()
{
    if (ImGui::BeginMenu("Engine"))
    {
        if (ImGui::MenuItem("Open Map"))
        {
            extern bool s_bToggleOpenMapFileDialog;
            s_bToggleOpenMapFileDialog = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Toggle Node Editor"))
        {
            m_bShowNodeEditor = !m_bShowNodeEditor;
        }
        if (ImGui::MenuItem("Take Profiling Capture"))
        {
            extern bool g_TriggerDumpProfilingCapture;
            extern std::string g_DumpProfilingCaptureFileName;
            g_DumpProfilingCaptureFileName = "Frames";
            g_TriggerDumpProfilingCapture = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Graphic"))
    {
        if (ImGui::MenuItem("Show Graphic Property Grid"))
        {
            m_bShowGraphicPropertyGrid = !m_bShowGraphicPropertyGrid;
        }

        ImGui::EndMenu();
    }


    if (ImGui::BeginMenu("Others"))
    {
        if (ImGui::MenuItem("Toggle IMGUI Demo Windows"))
        {
            m_bShowDemoWindows = !m_bShowDemoWindows;
        }

        ImGui::EndMenu();
    }
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

    if (m_bShowNodeEditor)
    {
        extern void UpdateNodeEditorWindow(bool& bWindowActive);
        UpdateNodeEditorWindow(m_bShowNodeEditor);
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

    // defined in World.cpp
    extern void UpdateSceneIMGUI();
    UpdateSceneIMGUI();

    if (ImGui::BeginMainMenuBar())
    {
        UpdateMainMenuBar();

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
