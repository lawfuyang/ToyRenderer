#include "Engine.h"

#include <dxgidebug.h>

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/imgui/imgui.h"
#include "extern/imgui/backends/imgui_impl_sdl3.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_keyboard.h"

#include "Graphic.h"
#include "Scene.h"
#include "Utilities.h"

#define SDL_CALL(x) if (!(x)) { LOG_DEBUG("SDL Error: %s", SDL_GetError()); assert(false); }

CommandLineOption<std::vector<int>> g_DisplayResolution{ "displayresolution", { 0, 0 } };
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

static Vector2U GetBestWindowSize()
{
    if (g_DisplayResolution.Get()[0] != 0 && g_DisplayResolution.Get()[1] != 0)
    {
        return { static_cast<uint32_t>(g_DisplayResolution.Get()[0]), static_cast<uint32_t>(g_DisplayResolution.Get()[1]) };
    }

    static const Vector2U kSizes[] =
    {
         { 3840, 2160 },
         { 2560, 1440 },
         { 1920, 1080 },
         { 1600, 900 },
         { 1280, 720 },
    };

    const SDL_DisplayID primaryDisplayID = SDL_GetPrimaryDisplay();
    SDL_CALL(primaryDisplayID);

    const SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(primaryDisplayID);
    SDL_CALL(displayMode);

    for (Vector2U size : kSizes)
    {
        if (size.x < displayMode->w && size.y < displayMode->h)
        {
            return size;
        }
    }

    assert(0); // there's nothing smaller than 720p on Steam Hardware Survey
    return kSizes[std::size(kSizes) - 1];
}

static bool IsWindowsDeveloperModeEnaable()
{
    // Look in the Windows Registry to determine if Developer Mode is enabled
    HKEY hKey;
    LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD keyValue, keySize = sizeof(DWORD);
    result = RegQueryValueEx(hKey, "AllowDevelopmentWithoutDevLicense", 0, NULL, (byte*)&keyValue, &keySize);

    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS) && (keyValue == 1);
}

std::string& Engine::GetDebugOutputStringForCurrentThread()
{
    thread_local std::string tl_DebugOutputString;
    return tl_DebugOutputString;
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

    m_bWindowsDeveloperMode = IsWindowsDeveloperModeEnaable();
    LOG_DEBUG("Windows Developer Mode: %s", m_bWindowsDeveloperMode ? "Enabled" : "Disabled");

    SDL_CALL(SDL_Init(SDL_INIT_VIDEO));

    m_WindowSize = GetBestWindowSize();
    LOG_DEBUG("Window Size: %d x %d", m_WindowSize.x, m_WindowSize.y);

    m_SDLWindow = SDL_CreateWindow("Toy Renderer", m_WindowSize.x, m_WindowSize.y, 0);
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

    std::string_view sceneToLoad = g_SceneToLoad.Get();
    assert(!sceneToLoad.empty());

    extern void LoadScene(std::string_view filePath);
    LoadScene(sceneToLoad);

    m_Graphic->PostSceneLoad();

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

#if 0
	// check for any leftover dxgi stuff
	ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
	{
		HRESULT_CALL(dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)));
	}
#endif
}

// https://blog.bearcats.nl/perfect-sleep-function/
static void TimerSleep(double microSeconds)
{
    using namespace std;
    using namespace chrono;
    static const int64_t kPeriod = 1;
    static const int64_t kTolerance = 1'020'000;

    auto t = high_resolution_clock::now();
    const auto target = t + nanoseconds(int64_t(microSeconds * 1e3));

    static HANDLE timer = ::CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    const int64_t maxTicks = kPeriod * 9'500;
    for (;;)
    {
        const int64_t remaining = (target - t).count();
        int64_t ticks = (remaining - kTolerance) / 100;
        if (ticks <= 0)
        {
            break;
        }
        if (ticks > maxTicks)
        {
            ticks = maxTicks;
        }

        LARGE_INTEGER due;
        due.QuadPart = -ticks;
        ::SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0);
        ::WaitForSingleObject(timer, INFINITE);
        t = high_resolution_clock::now();
    }

    // spin
    while (high_resolution_clock::now() < target)
    {
        YieldProcessor();
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

		if (m_FPSLimit != 0)
		{
            PROFILE_SCOPED("Busy Wait Until FPS Limit");

			const std::chrono::microseconds frameDuration{ 1000000 / m_FPSLimit };
            const auto timeLeft = frameDuration - std::chrono::microseconds{ frameTimer.GetElapsedMicroSeconds() };

            if (timeLeft.count() > 0)
            {
                TimerSleep(timeLeft.count());
            }
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
        if (ImGui::Begin("Graphic Properties", &bShowGraphicPropertyGrid, ImGuiWindowFlags_AlwaysAutoResize))
        {
            g_Scene->UpdateIMGUI();
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
