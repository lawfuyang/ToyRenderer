#include "World.h"

#include "extern/imgui/imgui.h"
#include "extern/portable-file-dialogs/portable-file-dialogs.h"

#include "CommonResources.h"
#include "Engine.h"
#include "Graphic.h"
#include "Mouse.h"
#include "Scene.h"
#include "Utilities.h"

void World::Initialize()
{
}

void World::Shutdown()
{
}

void World::CloseMap()
{
    g_Graphic.m_Scene->Shutdown();
}

void World::LoadMap()
{
    SCOPED_TIMER_FUNCTION();

    extern void LoadScene(std::string_view filePath);
    LoadScene(m_CurrentMapFileName);

    g_Graphic.m_Scene->OnSceneLoad();
}

// referenced in imguimanager
bool s_bToggleOpenMapFileDialog = false;

void World::UpdateIMGUI()
{
    if (s_bToggleOpenMapFileDialog)
    {
        std::vector<std::string> result = pfd::open_file("Select a map file", GetResourceDirectory(), { "All Files", "*" }, pfd::opt::force_path).result();
        if (!result.empty())
        {
            m_CurrentMapFileName = result[0];

            CloseMap();
            LoadMap();
        }
        s_bToggleOpenMapFileDialog = false;
    }
}

void World::Update()
{
    PROFILE_FUNCTION();

    Graphic::PickingContext& context = g_Graphic.m_PickingContext;

    if (context.m_State == Graphic::PickingContext::RESULT_READY)
    {
        if (context.m_Result != UINT_MAX)
        {
            Scene* scene = g_Graphic.m_Scene.get();

            const uint32_t pickedNodeID = context.m_Result;

            extern uint32_t g_CurrentlySelectedNodeID;
            g_CurrentlySelectedNodeID = pickedNodeID;

            assert(g_CurrentlySelectedNodeID < scene->m_Nodes.size());
        }

        context.m_State = Graphic::PickingContext::NONE;
    }

    if (context.m_State == Graphic::PickingContext::NONE && Mouse::WasButtonReleased(Mouse::Left) && !ImGui::GetIO().WantCaptureMouse)
    {
        g_Engine.AddCommand([&]
            {
                // TODO: properly scale mouse pos when we have upscaling
                const Vector2U clickPos{ std::min(g_Graphic.m_RenderResolution.x - 1, (uint32_t)Mouse::GetX()), std::min(g_Graphic.m_RenderResolution.y - 1, (uint32_t)Mouse::GetY()) };
                context.m_PickingLocation = clickPos;
                context.m_State = Graphic::PickingContext::REQUESTED;

                //LOG_DEBUG("Requested Picking: [%d, %d]", clickPos.x, clickPos.y);
            });
    }
}

// referenced in imguimanager
void UpdateWorldIMGUI()
{
    g_World.UpdateIMGUI();
}

// referenced in imguimanager
void CloseMap()
{
    g_World.CloseMap();
    g_World.m_CurrentMapFileName.clear();
}
