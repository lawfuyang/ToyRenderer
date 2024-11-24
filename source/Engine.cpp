#include "Engine.h"

#include <dxgidebug.h>

#include "extern/cxxopts/include/cxxopts.hpp"
#include "extern/SDL/SDL3/SDL.h"
#include "extern/SDL/SDL3/SDL_main.h"

#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "ImguiManager.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Scene.h"
#include "Utilities.h"

CommandLineOption<std::vector<int>> g_DisplayResolution{ "displayresolution", {1600, 900} };
CommandLineOption<bool> g_ProfileStartup{ "profilestartup", false };
CommandLineOption<int> g_MaxWorkerThreads{ "maxworkerthreads", 12 };
CommandLineOption<std::string> g_SceneToLoad{ "scene", "" };

thread_local uint32_t tl_ThreadID = 0;

void OnThreadCreateCB()
{
	static std::atomic<uint32_t> s_ThreadIDCounter = 0;
	tl_ThreadID = ++s_ThreadIDCounter;
	MicroProfileOnThreadCreate(StringFormat("Thread: [%d]", tl_ThreadID));
};

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

void Engine::Initialize(int argc, char** argv)
{
    SCOPED_TIMER_FUNCTION();
    PROFILE_FUNCTION();

    LOG_DEBUG("Root Directory: %s", GetRootDirectory());
    LOG_DEBUG("Executable Directory: %s", GetExecutableDirectory());
    LOG_DEBUG("Application Directory: %s", GetApplicationDirectory());

    ParseCommandlineArguments(argc, argv);

    // init background engine window thread for message windows pump
    new(&m_EngineWindowThread) std::thread{[this]() { RunEngineWindowThread(); }};

    MicroProfileOnThreadCreate("Main");
    MicroProfileSetEnableAllGroups(true);

    uint32_t nbWorkerThreads = g_MaxWorkerThreads.Get();
    nbWorkerThreads = nbWorkerThreads == 0 ? std::thread::hardware_concurrency() : nbWorkerThreads;

    // create threadpool executor
    m_Executor = std::make_shared<tf::Executor>(nbWorkerThreads);
    LOG_DEBUG("%d Worker Threads initialized", m_Executor->num_workers());

    // MT init tasks
    tf::Taskflow tf;
    tf.emplace([this] { m_Graphic = std::make_shared<Graphic>(); m_Graphic->Initialize(); });
    tf.emplace([this] { m_IMGUIManager = std::make_shared<IMGUIManager>(); m_IMGUIManager->Initialize(); });

    // MT init & wait
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

	m_IMGUIManager->ShutDown();
	m_IMGUIManager.reset();
	m_Graphic->Shutdown();
	m_Graphic.reset();

	m_EngineWindowThread.join();

	MicroProfileShutdown();

	// check for any leftover dxgi stuff
	ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
	{
		HRESULT_CALL(dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)));
	}
}

static void SleepCPUWhileWindowIsInactive()
{
    PROFILE_FUNCTION();

    DWORD ForegroundProcess{};
    ::GetWindowThreadProcessId(::GetForegroundWindow(), &ForegroundProcess);
    while (ForegroundProcess != ::GetCurrentProcessId() && !g_Engine.IsExiting())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &ForegroundProcess);
    }
}

static void BusyWaitUntilFPSLimit(Timer& timer)
{
    const auto& debugControllables = g_GraphicPropertyGrid.m_DebugControllables;

    const uint32_t fpsLimit = debugControllables.m_FPSLimit;
    if (fpsLimit == 0)
        return;

    const std::chrono::microseconds frameDuration{ 1000000 / fpsLimit };

    PROFILE_FUNCTION();

    while (timer.GetElapsedMicroSeconds() < frameDuration.count()) { YieldProcessor(); }
}

void Engine::MainLoop()
{
    LOG_DEBUG("Entering main loop");

    SCOPED_TIMER_FUNCTION();

    do
    {
        PROFILE_SCOPED("Frame");

        SleepCPUWhileWindowIsInactive();

        Timer frameTimer;

        {
            PROFILE_SCOPED("FPS-Capped Frame");

            // consume commands first at the very beginning of the frame
            ConsumeCommands();

            // for the sake of UI & property-editing stability, IMGUI must be updated in isolation single-threaded
            m_IMGUIManager->Update();

            m_Graphic->Update();

            if (Keyboard::IsKeyPressed(Keyboard::KEY_CTRL) && Keyboard::IsKeyPressed(Keyboard::KEY_SHIFT) && Keyboard::WasKeyPressed(Keyboard::KEY_COMMA))
            {
                TriggerDumpProfilingCapture("Frames");
            }

            // make sure I/O ticks happen last
            Keyboard::Tick();
            Mouse::Tick();
        }

        if (gs_TriggerDumpProfilingCapture)
        {
            DumpProfilingCapture();
        }

        m_CPUFrameTimeMs = frameTimer.GetElapsedMilliSeconds();
        BusyWaitUntilFPSLimit(frameTimer);
        m_CPUCappedFrameTimeMs = frameTimer.GetElapsedMilliSeconds();

        MicroProfileFlip(nullptr);
    } while (!m_Exit);

    LOG_DEBUG("Exiting main loop");
}

bool Engine::IsMainThread()
{
    return tl_ThreadID == 0;
}

uint32_t Engine::GetThreadID()
{
    return tl_ThreadID;
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

::LRESULT CALLBACK Engine::ProcessWindowsMessagePump(::HWND hWnd, ::UINT message, ::WPARAM wParam, ::LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:
        ::DestroyWindow(hWnd);
        g_Engine.m_Exit = true;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        g_Engine.m_Exit = true;
        break;

    case WM_MOUSEMOVE:
    {
        ::RECT rect;
        ::GetClientRect(g_Engine.m_WindowHandle, &rect);
        Mouse::ProcessMouseMove(lParam, rect);
        // NO BREAK HERE!
    }

    case WM_KEYUP: Keyboard::ProcessKeyUp((uint32_t)wParam); break;
    case WM_KEYDOWN: Keyboard::ProcessKeyDown((uint32_t)wParam); break;
    case WM_LBUTTONDOWN: Mouse::UpdateButton(Mouse::Left, true); break;
    case WM_LBUTTONUP: Mouse::UpdateButton(Mouse::Left, false); break;
    case WM_RBUTTONDOWN: Mouse::UpdateButton(Mouse::Right, true); break;
    case WM_RBUTTONUP: Mouse::UpdateButton(Mouse::Right, false); break;
    case WM_MOUSEWHEEL: Mouse::ProcessMouseWheel(wParam); break;
    }

    // imguimanager may have already been freed during shutdown phase
    if (g_Engine.m_IMGUIManager)
    {
        g_Engine.m_IMGUIManager->ProcessWindowsMessage(hWnd, message, wParam, lParam);
    }

    return ::DefWindowProc(hWnd, message, wParam, lParam);
}

void Engine::RunEngineWindowThread()
{
    const char* s_AppName = "ToyRenderer";

    ::HINSTANCE hInstance = 0;

    ::WNDCLASS wc = { 0 };
    wc.lpfnWndProc = ProcessWindowsMessagePump;
    wc.hInstance = hInstance;
    wc.hbrBackground = (::HBRUSH)(COLOR_BACKGROUND);
    wc.lpszClassName = s_AppName;

	auto CriticalWindowsError = []
        {
            DWORD err = GetLastError();
            LPTSTR lpBuffer = 0;
            FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpBuffer, 0, 0);
			LOG_DEBUG("Windows Error : %s", lpBuffer);
            LocalFree(lpBuffer);
            assert(0);
        };

    if (FAILED(RegisterClass(&wc)))
    {
        CriticalWindowsError();
    }

    const uint32_t kWindowWidth = g_DisplayResolution.Get()[0];
    const uint32_t kWindowHeight = g_DisplayResolution.Get()[1];

    const ::DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    ::RECT rect{ 0, 0, (LONG)kWindowWidth, (LONG)kWindowHeight };
    ::AdjustWindowRect(&rect, style, false);

    const int posX = (GetSystemMetrics(SM_CXSCREEN) - kWindowWidth) / 2;
    const int posY = (GetSystemMetrics(SM_CYSCREEN) - kWindowHeight) / 2;

    ::HWND engineWindowHandle = CreateWindow(wc.lpszClassName,
        s_AppName,
        style,
        posX, posY,
        rect.right - rect.left,
        rect.bottom - rect.top,
        0, 0, hInstance, NULL);

    if (engineWindowHandle == 0)
    {
        CriticalWindowsError();
    }

    m_WindowHandle = engineWindowHandle;

    ::ShowCursor(true);
    ::SetCursor(::LoadCursor(NULL, IDC_ARROW));

    ::MSG msg;
    ::BOOL bRet;
    while ((bRet = ::GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            CriticalWindowsError();
        }
        else
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    LOG_DEBUG("Leaving Engine Window Thread");
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

static std::string gs_ExecutableDirectory;
const char* GetExecutableDirectory()
{
    return gs_ExecutableDirectory.c_str();
}

int SDL_main(int argc, char** argv)
{
    gs_ExecutableDirectory = std::filesystem::path{ argv[0] }.parent_path().string();

#if 1
    Engine e;
    e.Initialize(argc, argv);
    e.MainLoop();
    e.Shutdown();
#else
    SDL_Init(SDL_INIT_EVENTS);
    SDL_Window* window = SDL_CreateWindow("Hello SDL", g_DisplayResolution.Get()[0], g_DisplayResolution.Get()[1], 0);

    bool bQuit = false;
    SDL_Event e;
    while (!bQuit)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
            {
                bQuit = true;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
#endif
    return 0;
}
