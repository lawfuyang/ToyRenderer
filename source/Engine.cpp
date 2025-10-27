#include "Engine.h"

#include <dxgidebug.h>
#include <psapi.h>

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/imgui/imgui.h"
#include "extern/imgui/backends/imgui_impl_sdl3.h"

#include "SDL3/SDL_main.h"
#include "SDL3/SDL_keyboard.h"

#include "Graphic.h"
#include "Scene.h"
#include "Utilities.h"

CommandLineOption<std::vector<int>> g_DisplayResolution{ "displayresolution", { 0, 0 } };
CommandLineOption<bool> g_ProfileStartup{ "profilestartup", false };
CommandLineOption<int> g_MaxWorkerThreads{ "maxworkerthreads", 12 };

static bool gs_TriggerDumpProfilingCapture = false;
static std::string gs_DumpProfilingCaptureFileName;

static void DumpProfilingCapture()
{
    check(!gs_DumpProfilingCaptureFileName.empty());

    const std::string fileName = (std::filesystem::path{ GetExecutableDirectory() } / gs_DumpProfilingCaptureFileName.c_str()).string() + ".html";
    SDL_Log("Dumping profiler log: %s", fileName.c_str());

    MicroProfileDumpFileImmediately(fileName.c_str(), nullptr, nullptr);

    gs_DumpProfilingCaptureFileName.clear();
    gs_TriggerDumpProfilingCapture = false;
}

void TriggerDumpProfilingCapture(std::string_view fileName)
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

    check(0); // there's nothing smaller than 720p on Steam Hardware Survey
    return kSizes[std::size(kSizes) - 1];
}

void Engine::Initialize(int argc, char** argv)
{
    SCOPED_TIMER_FUNCTION();
    PROFILE_FUNCTION();

    gs_ExecutableDirectory = std::filesystem::path{ argv[0] }.parent_path().string();

    SDL_Log("Root Directory: %s", GetRootDirectory());
    SDL_Log("Executable Directory: %s", GetExecutableDirectory());
    SDL_Log("Application Directory: %s", GetApplicationDirectory());

    ParseCommandlineArguments(argc, argv);

    SDL_CALL(SDL_Init(SDL_INIT_VIDEO));

    m_WindowSize = GetBestWindowSize();
    SDL_Log("Window Size: %d x %d", m_WindowSize.x, m_WindowSize.y);

    m_SDLWindow = SDL_CreateWindow("Toy Renderer", m_WindowSize.x, m_WindowSize.y, 0);
    SDL_CALL(m_SDLWindow);

    SDL_CALL(SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED));
    SDL_CALL(SDL_ShowWindow(m_SDLWindow));

    MicroProfileOnThreadCreate("Main");
    MicroProfileSetEnableAllGroups(true);

    uint32_t nbWorkerThreads = g_MaxWorkerThreads.Get();
    nbWorkerThreads = nbWorkerThreads == 0 ? std::thread::hardware_concurrency() : nbWorkerThreads;
    nbWorkerThreads = std::min<uint32_t>(MICROPROFILE_MAX_THREADS - 1, nbWorkerThreads);

    // create threadpool executor
    m_Executor = std::make_shared<tf::Executor>(nbWorkerThreads);
    SDL_Log("%d Worker Threads initialized", m_Executor->num_workers());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    
    verify(ImGui_ImplSDL3_InitForD3D(m_SDLWindow));

    extern void PreloadScene();
    extern void LoadScene();

    m_Graphic = std::make_shared<Graphic>();
    m_Graphic->m_Scene = std::make_shared<Scene>();

    tf::Taskflow tf;
    tf.emplace([this] { m_Graphic->Initialize(); });
    tf.emplace([this] { PreloadScene(); });
    m_Executor->run(tf).wait();

    LoadScene();

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
    SDL_Log(printArgsStr.c_str());

    if (!parseResult.unmatched().empty())
    {
        printArgsStr = "Unmatched Command Line Arguments: { ";
        for (std::string_view s : parseResult.unmatched())
        {
            printArgsStr += StringFormat("%s ", s.data());
        }
        printArgsStr += "}";
        SDL_Log(printArgsStr.c_str());
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
    SDL_Log("Entering main loop");

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

        m_CPUFrameTimeMs = frameTimer.GetElapsedMilliseconds();

		if (m_FPSLimit != 0)
		{
            PROFILE_SCOPED("Busy Wait Until FPS Limit");

            const uint64_t frameDurationNanoSeconds = 1000000.0f / m_FPSLimit;
            if (frameTimer.GetElapsedNanoseconds() < frameDurationNanoSeconds)
            {
                const uint64_t timeLeftNanoSeconds = frameDurationNanoSeconds - frameTimer.GetElapsedNanoseconds();
                SDL_DelayPrecise(timeLeftNanoSeconds);
            }
		}

        m_CPUCappedFrameTimeMs = frameTimer.GetElapsedMilliseconds();

        MicroProfileFlip(nullptr);
    } while (!m_Exit);

    SDL_Log("Exiting main loop");
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

        // TODO: move this to platform specific API?
        PROCESS_MEMORY_COUNTERS processMemoryCounters{};
        ::GetProcessMemoryInfo(::GetCurrentProcess(), &processMemoryCounters, sizeof(processMemoryCounters));

        ImGui::Text("\tCPU: [%5.2f ms]", m_CPUFrameTimeMs);
        ImGui::Text("\tCPU (Graphic): [%5.2f ms]", m_Graphic->m_GraphicUpdateTimerMs);
        ImGui::Text("\tGPU: [%5.2f ms]", m_GPUTimeMs);
        ImGui::Text("\tSysMem: [%.2f MB]", BYTES_TO_MB(processMemoryCounters.WorkingSetSize));
        ImGui::Text("\tVRAM: [%.2f MB]", BYTES_TO_MB(g_Graphic.m_GraphicRHI->GetUsedVideoMemory()));
        ImGui::Text("\tFPS: [%.0f]", 1000.0f / std::max((float)m_CPUFrameTimeMs, m_GPUTimeMs));

        ImGui::EndMainMenuBar();
    }
}

int SDL_main(int argc, char** argv)
{
    Engine e;
    e.Initialize(argc, argv);
    e.MainLoop();
    e.Shutdown();

    return 0;
}
