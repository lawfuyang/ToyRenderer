#include "Engine.h"

#include <dxgidebug.h>

#include "extern/cxxopts/cxxopts.hpp"

#include "Graphic.h"
#include "GraphicPropertyGrid.h"
#include "ImguiManager.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Utilities.h"

CommandLineOption<std::vector<int>> g_DisplayResolution{ "displayresolution", {1600, 900} };
CommandLineOption<bool> g_ProfileStartup{ "profilestartup", false };
CommandLineOption<int> g_MaxWorkerThreads{ "maxworkerthreads", 12 };

thread_local uint32_t tl_ThreadID = 0;

void OnThreadCreateCB()
{
	static std::atomic<uint32_t> s_ThreadIDCounter = 0;
	tl_ThreadID = ++s_ThreadIDCounter;
	MicroProfileOnThreadCreate(StringFormat("Thread: [%d]", tl_ThreadID));
};

bool g_TriggerDumpProfilingCapture = false;
std::string g_DumpProfilingCaptureFileName;

void DumpProfilingCapture()
{
    assert(!g_DumpProfilingCaptureFileName.empty());

    const std::string fileName = (std::filesystem::path{ GetExecutableDirectory() } / g_DumpProfilingCaptureFileName.c_str()).string() + ".html";
    LOG_DEBUG("Dumping profiler log: %s", fileName.c_str());

    static uint32_t s_ProfilercaptureFrames = 30;
    MicroProfileDumpFileImmediately(fileName.c_str(), nullptr, nullptr, s_ProfilercaptureFrames);

    g_DumpProfilingCaptureFileName.clear();
    g_TriggerDumpProfilingCapture = false;
}

void Engine::Initialize()
{
    SCOPED_TIMER_FUNCTION();
    PROFILE_FUNCTION();

    LOG_DEBUG("Executable Directory: %s", GetExecutableDirectory());
    LOG_DEBUG("Application Directory: %s", GetApplicationDirectory());
    LOG_DEBUG("Resources Directory: %s", GetResourceDirectory());

    // Look in the Windows Registry to determine if Developer Mode is enabled
    {
        HKEY hKey;
        LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock", 0, KEY_READ, &hKey);
        if (result == ERROR_SUCCESS)
        {
            DWORD keyValue, keySize = sizeof(DWORD);
            result = RegQueryValueEx(hKey, "AllowDevelopmentWithoutDevLicense", 0, NULL, (byte*)&keyValue, &keySize);
            m_bDeveloperModeEnabled = (result == ERROR_SUCCESS && keyValue == 1);
            RegCloseKey(hKey);
        }

        LOG_DEBUG("Windows Developer Mode: [%d]", m_bDeveloperModeEnabled);
    }
    
    ParseCommandlineArguments();

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

    if (g_ProfileStartup.Get())
    {
        g_DumpProfilingCaptureFileName = "EngineInit";
        DumpProfilingCapture();
    }
}

void Engine::ParseCommandlineArguments()
{
    cxxopts::Options options{ __argv[0], "Argument Parser" };

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

    const cxxopts::ParseResult parseResult = options.parse(__argc, (const char**)__argv);

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
                g_DumpProfilingCaptureFileName = "Frames";
                g_TriggerDumpProfilingCapture = true;
            }

            // make sure I/O ticks happen last
            Keyboard::Tick();
            Mouse::Tick();
        }

        float elapsedMS = frameTimer.GetElapsedMilliSeconds();
        m_CPUFrameTimeMs = elapsedMS;

        m_CPUFrameTimeMs = frameTimer.GetElapsedMilliSeconds();

        BusyWaitUntilFPSLimit(frameTimer);

        if (g_TriggerDumpProfilingCapture)
        {
            DumpProfilingCapture();
        }

        elapsedMS = frameTimer.GetElapsedMilliSeconds();
        m_CPUCappedFrameTimeMs = elapsedMS;

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

    if (FAILED(RegisterClass(&wc)))
    {
        LOG_DEBUG("ApplicationWin : Failed to create window: %s", GetLastErrorAsString());
        assert(false);
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
        LOG_DEBUG("ApplicationWin : Failed to create window: %s", GetLastErrorAsString());
        assert(false);
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
            LOG_DEBUG("Can't get new message: %s", GetLastErrorAsString());
            assert(false);
        }
        else
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    LOG_DEBUG("Leaving Engine Window Thread");
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    Engine e;
    e.Initialize();
    e.MainLoop();
    e.Shutdown();
    return 0;
}
