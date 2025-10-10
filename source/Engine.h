#pragma once

#include "extern/microprofile/microprofile.h"
#include "extern/taskflow/taskflow/taskflow.hpp"

#include "CriticalSection.h"
#include "MathUtilities.h"

class Graphic;

#define SDL_CALL(x) if (!(x)) { SDL_Log("SDL Error: %s", SDL_GetError()); check(false); }

// forward declare 'StringFormat' here so that logging macros can compile without including Utilities.h
const char* StringFormat(const char* format, ...);

class Engine
{
    SingletonFunctionsSimple(Engine);

public:
    void Initialize(int argc, char** argv);
    void Shutdown();
    void MainLoop();

    template <typename Lambda> void AddCommand(Lambda&& lambda) { AUTO_LOCK(m_CommandsLock); m_PendingCommands.push_back(lambda); }
    template <typename Lambda> void AddCommand(Lambda& lambda) { static_assert(sizeof(Lambda) == 0); /* enforce use of rvalue and therefore move to avoid an extra copy of the Lambda */ }

    uint32_t m_FPSLimit = 200;

    float m_CPUFrameTimeMs = 16.6f;
    float m_CPUCappedFrameTimeMs = 16.6f;
    float m_GPUTimeMs = 16.6f;

	struct SDL_Window* m_SDLWindow = nullptr;
    Vector2U m_WindowSize;

    std::shared_ptr<tf::Executor> m_Executor;

    float m_MouseWheelY = 0.0f;
private:
    void ParseCommandlineArguments(int argc, char** argv);
    void ConsumeCommands();
    void UpdateIMGUI();

    bool m_Exit = false;
    std::shared_ptr<Graphic> m_Graphic;

    SpinLock m_CommandsLock;
    std::vector<std::function<void()>> m_PendingCommands;    
};
#define g_Engine Engine::GetInstance()

// note: DON'T input formatted strings from 'StringFormat'!!! It will cock up the profiling dump if the internal ring buffer of strings gets overwritten
#define PROFILE_SCOPED(NAME) MICROPROFILE_SCOPE_CSTR(NAME)

#define PROFILE_FUNCTION() PROFILE_SCOPED(__FUNCTION__)

template <typename T>
class CommandLineOption
{
public:
    CommandLineOption(const char* opts, T defaultValue)
        : value(defaultValue)
    {
        auto[insertIt, bInserted] = ms_CachedArgs.insert({opts, &value});

        check(bInserted); // cmd line arg already exists
    }

    const T& Get() const { return value; }

private:
    T value;

    inline static std::unordered_map<std::string, T*> ms_CachedArgs;

    friend class Engine;
};
