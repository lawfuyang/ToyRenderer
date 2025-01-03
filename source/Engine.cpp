#include "Engine.h"

#include <dxgidebug.h>

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/SDL/SDL3/SDL.h"
#include "extern/SDL/SDL3/SDL_main.h"
#include "extern/SDL/SDL3/SDL_keyboard.h"
#include "extern/imgui/imgui.h"
#include "extern/imgui/backends/imgui_impl_sdl3.h"

#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "Scene.h"
#include "Utilities.h"

#define SDL_CALL(x) if (!(x)) { LOG_DEBUG("SDL Error: %s", SDL_GetError()); assert(false); }

CommandLineOption<std::vector<int>> g_DisplayResolution{ "displayresolution", {1600, 900} };
CommandLineOption<bool> g_ProfileStartup{ "profilestartup", false };
CommandLineOption<int> g_MaxWorkerThreads{ "maxworkerthreads", 12 };
CommandLineOption<std::string> g_SceneToLoad{ "scene", "" };

static bool gs_TriggerDumpProfilingCapture = false;
static std::string gs_DumpProfilingCaptureFileName;

static void DumpProfilingCapture()
{
    assert(!gs_DumpProfilingCaptureFileName.empty());

    const std::string fileName = (std::filesystem::path{ GetExecutableDirectory() } / gs_DumpProfilingCaptureFileName.c_str()).string() + ".html";
    LOG_DEBUG("Dumping profiler log: %s", fileName.c_str());

    MicroProfileDumpFileImmediately(fileName.c_str(), nullptr, nullptr);

    gs_DumpProfilingCaptureFileName.clear();
    gs_TriggerDumpProfilingCapture = false;
}

static void TriggerDumpProfilingCapture(std::string_view fileName)
{
    gs_TriggerDumpProfilingCapture = true;
    gs_DumpProfilingCaptureFileName = fileName;
}

static std::string gs_ExecutableDirectory;
const char* GetExecutableDirectory()
{
    return gs_ExecutableDirectory.c_str();
}

void Engine::Initialize(int argc, char** argv)
{
    SCOPED_TIMER_FUNCTION();
    PROFILE_FUNCTION();

    gs_ExecutableDirectory = std::filesystem::path{ argv[0] }.parent_path().string();

    LOG_DEBUG("Root Directory: %s", GetRootDirectory());
    LOG_DEBUG("Executable Directory: %s", GetExecutableDirectory());
    LOG_DEBUG("Application Directory: %s", GetApplicationDirectory());

    ParseCommandlineArguments(argc, argv);

    SDL_CALL(SDL_Init(SDL_INIT_EVENTS));
    m_SDLWindow = SDL_CreateWindow("Toy Renderer", g_DisplayResolution.Get()[0], g_DisplayResolution.Get()[1], 0);
    SDL_CALL(m_SDLWindow);

    SDL_CALL(SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED));
    SDL_CALL(SDL_ShowWindow(m_SDLWindow));

    MicroProfileOnThreadCreate("Main");
    MicroProfileSetEnableAllGroups(true);

    uint32_t nbWorkerThreads = g_MaxWorkerThreads.Get();
    nbWorkerThreads = nbWorkerThreads == 0 ? std::thread::hardware_concurrency() : nbWorkerThreads;

    // create threadpool executor
    m_Executor = std::make_shared<tf::Executor>(nbWorkerThreads);
    LOG_DEBUG("%d Worker Threads initialized", m_Executor->num_workers());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    verify(ImGui_ImplSDL3_InitForD3D(m_SDLWindow));

    tf::Taskflow tf;
    tf.emplace([this] { m_Graphic = std::make_shared<Graphic>(); m_Graphic->Initialize(); });
    m_Executor->run(tf).wait();

	if (std::string_view sceneToLoad = g_SceneToLoad.Get();
        !sceneToLoad.empty())
    {
        extern void LoadScene(std::string_view filePath);
        LoadScene(sceneToLoad);

        m_Graphic->m_Scene->OnSceneLoad();
    }

    if (g_ProfileStartup.Get())
    {
        TriggerDumpProfilingCapture("EngineInit");
    }
}

void Engine::ParseCommandlineArguments(int argc, char** argv)
{
    cxxopts::Options options{ argv[0], "Argument Parser" };

    options.allow_unrecognised_options();

    auto RegisterCmdLineOptsMap = [&](const auto& optsMap)
    {
        for (const auto& [opts, val] : optsMap)
        {
            options.add_options() (opts.c_str(), "", cxxopts::value(*val));
        }
    };

    // TODO: add more types as needed
    RegisterCmdLineOptsMap(CommandLineOption<bool>::ms_CachedArgs);
    RegisterCmdLineOptsMap(CommandLineOption<int>::ms_CachedArgs);
    RegisterCmdLineOptsMap(CommandLineOption<float>::ms_CachedArgs);
    RegisterCmdLineOptsMap(CommandLineOption<std::vector<int>>::ms_CachedArgs);
    RegisterCmdLineOptsMap(CommandLineOption<std::string>::ms_CachedArgs);

    const cxxopts::ParseResult parseResult = options.parse(argc, argv);

    std::string printArgsStr = "Command Line Arguments: ";
    for (const cxxopts::KeyValue& arg : parseResult.arguments())
    {
        printArgsStr += StringFormat("{%s : %s} ", arg.key().c_str(), arg.value().c_str());
    }
    LOG_DEBUG(printArgsStr.c_str());

    if (!parseResult.unmatched().empty())
    {
        std::string printArgsStr = "Unmatched Command Line Arguments: { ";
        for (std::string_view s : parseResult.unmatched())
        {
            printArgsStr += StringFormat("%s ", s.data());
        }
        printArgsStr += "}";
        LOG_DEBUG(printArgsStr.c_str());
    }
}

void Engine::Shutdown()
{
	SCOPED_TIMER_FUNCTION();

	// recurssive consume all commands until empty
	while (!m_PendingCommands.empty())
	{
		ConsumeCommands();
	}

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

	m_Graphic->Shutdown();
	m_Graphic.reset();

	MicroProfileShutdown();

    SDL_DestroyWindow(m_SDLWindow);
    SDL_Quit();

	// check for any leftover dxgi stuff
	ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
	{
		HRESULT_CALL(dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)));
	}
}

void Engine::MainLoop()
{
    LOG_DEBUG("Entering main loop");

    SCOPED_TIMER_FUNCTION();

    do
    {
        PROFILE_SCOPED("Frame");

        Timer frameTimer;

        {
            PROFILE_SCOPED("FPS-Capped Frame");

            // consume commands first at the very beginning of the frame
            ConsumeCommands();

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    m_Exit = true;
                }
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_SDLWindow))
                {
                    m_Exit = true;
                }

				// ghetto way to store mouse wheel input
                if (event.type == SDL_EVENT_MOUSE_WHEEL)
                {
					m_MouseWheelY = event.wheel.y;
                }

                ImGui_ImplSDL3_ProcessEvent(&event);
            }

            // sleep cpu if window is inactive
            // NOTE: windowFlag will be '0' without any mouse or keyboard input
            const SDL_WindowFlags windowFlags = SDL_GetWindowFlags(g_Engine.m_SDLWindow);
            if (windowFlags == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
                continue;
            }

            // for the sake of UI & property-editing stability, IMGUI must be updated in isolation single-threaded
            UpdateIMGUI();

            tf::Taskflow tf;
            tf.emplace([this] { m_Graphic->Update(); });
            m_Executor->run(tf).wait();

			const SDL_Keymod keyMod = SDL_GetModState();
			const bool* keyboardStates = SDL_GetKeyboardState(nullptr);

            if ((keyMod & SDL_KMOD_LCTRL) &&
                (keyMod & SDL_KMOD_LSHIFT) &&
                keyboardStates[SDL_SCANCODE_COMMA])
            {
                TriggerDumpProfilingCapture("Frames");
            }

			// reset mouse wheel input
			m_MouseWheelY = 0.0f;
        }

        if (gs_TriggerDumpProfilingCapture)
        {
            DumpProfilingCapture();
        }

        m_CPUFrameTimeMs = frameTimer.GetElapsedMilliSeconds();

		if (const uint32_t fpsLimit = g_GraphicPropertyGrid.m_DebugControllables.m_FPSLimit;
            fpsLimit != 0)
		{
			const std::chrono::microseconds frameDuration{ 1000000 / fpsLimit };

			PROFILE_SCOPED("Busy Wait Until FPS Limit");
			while (frameTimer.GetElapsedMicroSeconds() < frameDuration.count()) { YieldProcessor(); }
		}

        m_CPUCappedFrameTimeMs = frameTimer.GetElapsedMilliSeconds();

        MicroProfileFlip(nullptr);
    } while (!m_Exit);

    LOG_DEBUG("Exiting main loop");
}

void Engine::ConsumeCommands()
{
    PROFILE_FUNCTION();
    STATIC_MULTITHREAD_DETECTOR();
    
    // NOTE: commands are not allowed to MT via "corun", it will assert

    std::vector<std::function<void()>> executingCommands = std::move(m_PendingCommands);
    for (const std::function<void()>& cmd : executingCommands)
    {
        PROFILE_SCOPED("Engine Command");
        cmd();
    }
}

void Engine::UpdateIMGUI()
{
    PROFILE_FUNCTION();

	ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    // Show all IMGUI widget demos
    static bool bShowDemoWindows = false;
    if (bShowDemoWindows)
    {
        ImGui::ShowDemoWindow();
    }

	static bool bShowGraphicPropertyGrid = true;
    if (bShowGraphicPropertyGrid)
    {
        if (ImGui::Begin("Graphic Property Grid", &bShowGraphicPropertyGrid, ImGuiWindowFlags_AlwaysAutoResize))
        {
            g_GraphicPropertyGrid.UpdateIMGUI();
        }
        ImGui::End();
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Menu"))
        {
            if (ImGui::MenuItem("Show Graphic Property Grid"))
            {
                bShowGraphicPropertyGrid = !bShowGraphicPropertyGrid;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Toggle IMGUI Demo Windows"))
            {
                bShowDemoWindows = !bShowDemoWindows;
            }

            ImGui::EndMenu();
        }

        ImGui::Text("\tCPU: [%.2f ms]", m_CPUFrameTimeMs);
        ImGui::SameLine();
        ImGui::Text("\tGPU: [%.2f] ms", m_GPUTimeMs);
        ImGui::Text("\tFPS: [%.1f]", 1000.0f / std::max((float)m_CPUFrameTimeMs, m_GPUTimeMs));

        ImGui::EndMainMenuBar();
    }
}

#if ENABLE_MEM_LEAK_DETECTION

#define STB_LEAKCHECK_IMPLEMENTATION
#include "extern/stb/stb_leakcheck.h"

struct LeakDetector
{
	~LeakDetector()
    {
        stb_leakcheck_dumpmem();
	}
};
static LeakDetector gs_LeakDetector;
#endif // ENABLE_MEM_LEAK_DETECTION

int SDL_main(int argc, char** argv)
{
    Engine e;
    e.Initialize(argc, argv);
    e.MainLoop();
    e.Shutdown();

    return 0;
}
