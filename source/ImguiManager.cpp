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

    // This will back up the render data until the next imgui draw call
    SaveDrawData();
}

void IMGUIManager::SaveDrawData()
{
    PROFILE_FUNCTION();

    ImGui::Render();

    ImDrawData* imguiDrawData = ImGui::GetDrawData();
    assert(imguiDrawData);

    IMGUIDrawData newDrawData;
    newDrawData.m_VtxCount = imguiDrawData->TotalVtxCount;
    newDrawData.m_IdxCount = imguiDrawData->TotalIdxCount;
    newDrawData.m_Pos = Vector2{ imguiDrawData->DisplayPos.x, imguiDrawData->DisplayPos.y };
    newDrawData.m_Size = Vector2{ imguiDrawData->DisplaySize.x, imguiDrawData->DisplaySize.y };

    newDrawData.m_DrawList.resize(imguiDrawData->CmdListsCount);
    for (int n = 0; n < imguiDrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = imguiDrawData->CmdLists[n];

        // Copy VB and IB
        newDrawData.m_DrawList[n].m_VB.resize(cmd_list->VtxBuffer.size());
        memcpy(newDrawData.m_DrawList[n].m_VB.data(), &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof(ImDrawVert));
        newDrawData.m_DrawList[n].m_IB.resize(cmd_list->IdxBuffer.size());
        memcpy(newDrawData.m_DrawList[n].m_IB.data(), &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx));

        // Copy cmdlist params
        newDrawData.m_DrawList[n].m_DrawCmd.resize(cmd_list->CmdBuffer.size());
        memcpy(newDrawData.m_DrawList[n].m_DrawCmd.data(), &cmd_list->CmdBuffer[0], cmd_list->CmdBuffer.size() * sizeof(ImDrawCmd));
    }

    m_PendingDrawData = std::move(newDrawData);
}
