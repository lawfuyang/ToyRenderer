#pragma once

#include "extern/microprofile/microprofile.h"
#include "extern/taskflow/taskflow/taskflow.hpp"

#include "CriticalSection.h"

class Graphic;
class IMGUIManager;

// forward declare 'StringFormat' here so that logging macros can compile without including Utilities.h
const char* StringFormat(const char* format, ...);

enum class LogLevel { Debug, Warning, Error };

class Engine
{
    SingletonFunctionsSimple(Engine);

public:
    void Initialize(int argc, char** argv);
    void Shutdown();
    void MainLoop();

    static bool IsMainThread();
    static uint32_t GetThreadID();
    
	bool IsExiting() const { return m_Exit; }

    template <typename Lambda> void AddCommand(Lambda&& lambda) { AUTO_LOCK(m_CommandsLock); m_PendingCommands.push_back(lambda); }
    template <typename Lambda> void AddCommand(Lambda& lambda) { static_assert(sizeof(Lambda) == 0); /* enforce use of rvalue and therefore move to avoid an extra copy of the Lambda */ }

    float m_CPUFrameTimeMs = 0.0f;
    float m_CPUCappedFrameTimeMs = 0.0f;
    float m_GPUTimeMs = 0.0f;

    ::HWND m_WindowHandle = nullptr;
    std::shared_ptr<tf::Executor> m_Executor;
    std::shared_ptr<IMGUIManager> m_IMGUIManager;

private:
    static ::LRESULT CALLBACK ProcessWindowsMessagePump(::HWND hWnd, ::UINT message, ::WPARAM wParam, ::LPARAM lParam);
    void CreateAppWindow();
    void ParseCommandlineArguments(int argc, char** argv);
    void ConsumeCommands();

    bool m_Exit = false;
    std::shared_ptr<Graphic> m_Graphic;

    SpinLock m_CommandsLock;
    std::vector<std::function<void()>> m_PendingCommands;

    friend class IMGUIManager;
};
#define g_Engine Engine::GetInstance()
#define g_IMGUIManager (*(g_Engine.m_IMGUIManager))

inline thread_local HRESULT tl_HResult;
#define HRESULT_CALL(call)           \
    {                                \
        tl_HResult = (call);         \
        assert(!FAILED(tl_HResult)); \
    }

// note: DON'T input formatted strings from 'StringFormat'!!! It will cock up the profiling dump if the internal ring buffer of strings gets overwritten
#define PROFILE_SCOPED(NAME) MICROPROFILE_SCOPE_CSTR(NAME)

#define PROFILE_FUNCTION() PROFILE_SCOPED(__FUNCTION__)

#define LOG_DEBUG(FORMAT, ...)                                    \
{                                                                 \
    const char* formattedStr = StringFormat(FORMAT, __VA_ARGS__); \
    const char* finalStr = StringFormat("%s\n", formattedStr);    \
    OutputDebugString(finalStr);                                  \
}

template <typename T>
class CommandLineOption
{
public:
    CommandLineOption(const char* opts, T defaultValue)
        : value(defaultValue)
    {
        auto[insertIt, bInserted] = ms_CachedArgs.insert({opts, &value});

        assert(bInserted); // cmd line arg already exists
    }

    const T& Get() const { return value; }

private:
    T value;

    inline static std::unordered_map<std::string, T*> ms_CachedArgs;

    friend class Engine;
};
