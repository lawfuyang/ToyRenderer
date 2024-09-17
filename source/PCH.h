#pragma once

// C Lib
#include <assert.h>
#include <stdint.h>
#include <string.h>

// C++ Lib
#include <algorithm>
#include <array>
#include <bitset>
#include <codecvt>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Windows
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>

#define ENABLE_MEM_LEAK_DETECTION _DEBUG && 0

#if ENABLE_MEM_LEAK_DETECTION
#include "extern/stb/stb_leakcheck.h"
#endif

#define PRAGMA_OPTIMIZE_OFF __pragma(optimize("",off))
#define PRAGMA_OPTIMIZE_ON  __pragma(optimize("", on))

using Microsoft::WRL::ComPtr;

#define SingletonFunctionsCommon(ClassName)          \
    ClassName(const ClassName&)            = delete; \
    ClassName(ClassName&&)                 = delete; \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName& operator=(ClassName&&)      = delete; \
    inline static ClassName* ms_Instance   = nullptr;

#define SingletonFunctionsMeyers(ClassName) \
private:                                    \
    SingletonFunctionsCommon(ClassName);    \
public:                                     \
    ClassName() { ms_Instance = this; }     \
    static ClassName& GetInstance() { static ClassName s_Instance; return s_Instance; }



#define SingletonFunctionsSimple(ClassName)                      \
private:                                                         \
    SingletonFunctionsCommon(ClassName);                         \
public:                                                          \
    ClassName() { assert(!ms_Instance); ms_Instance = this; }    \
    ~ClassName() { assert(ms_Instance); ms_Instance = nullptr; } \
    static ClassName& GetInstance() { assert(ms_Instance); return *ms_Instance; }

#define TOSTRING_INTERNAL(x)               #x
#define TOSTRING(x)                        TOSTRING_INTERNAL(x)
#define FILE_AND_LINE                      __FILE__ "(" TOSTRING(__LINE__) ")"
#define JOIN_MACROS_INTERNAL( Arg1, Arg2 ) Arg1##Arg2
#define JOIN_MACROS( Arg1, Arg2 )          JOIN_MACROS_INTERNAL( Arg1, Arg2 )
#define GENERATE_UNIQUE_VARIABLE(basename) JOIN_MACROS(basename, __COUNTER__)

template<class T>
class MemberAutoUnset
{
    T& m_MemberRef;
    T   m_BackupVal;

public:
    MemberAutoUnset(T& member, T value)
        : m_MemberRef(member)
    {
        m_BackupVal = m_MemberRef;
        m_MemberRef = value;
    }
    ~MemberAutoUnset()
    {
        m_MemberRef = m_BackupVal;
    }
};

template <typename EnterLambda, typename ExitLamda>
class AutoScopeCaller
{
public:
    AutoScopeCaller(EnterLambda&& enterLambda, ExitLamda&& exitLambda)
        : m_ExitLambda(exitLambda)
    {
        enterLambda();
    }

    ~AutoScopeCaller()
    {
        m_ExitLambda();
    }

private:
    ExitLamda m_ExitLambda;
};

#define AUTO_SCOPE(enterLambda, exitLambda) const AutoScopeCaller GENERATE_UNIQUE_VARIABLE(AutoOnExitVar){ enterLambda, exitLambda };
#define ON_EXIT_SCOPE_LAMBDA(lambda)        const AutoScopeCaller GENERATE_UNIQUE_VARIABLE(AutoOnExitVar){ []{}, lambda };
#define SCOPED_UNSET(var, val)              MemberAutoUnset<decltype(var)> GENERATE_UNIQUE_VARIABLE(autoUnset){var, val};
#define MEM_ZERO_ARRAY(dst)                 memset(dst, 0, sizeof(dst))
#define MEM_ZERO_STRUCT(dst)                memset(&dst, 0, sizeof(dst))

// Sizes for convenience...
#define KB_TO_BYTES(Nb)             ((Nb)*1024ULL)                     // Define for kilobytes prefix (2 ^ 10)
#define MB_TO_BYTES(Nb)             KB_TO_BYTES((Nb)*1024ULL)          // Define for megabytes prefix (2 ^ 20)
#define GB_TO_BYTES(Nb)             MB_TO_BYTES((Nb)*1024ULL)          // Define for gigabytes prefix (2 ^ 30)
#define BYTES_TO_KB(Nb)             ((Nb) * (1.0f / 1024))             // Define to convert bytes to kilobyte
#define BYTES_TO_MB(Nb)             (BYTES_TO_KB(Nb) * (1.0f / 1024))  // Define to convert bytes to megabytes

namespace CompileTimeHashInternal
{
    constexpr uint32_t Hash32_CT(const char* str, size_t n, uint32_t basis = UINT32_C(2166136261)) {
        return n == 0 ? basis : Hash32_CT(str + 1, n - 1, (basis ^ str[0]) * UINT32_C(16777619));
    }

    constexpr uint64_t Hash64_CT(const char* str, size_t n, uint64_t basis = UINT64_C(14695981039346656037)) {
        return n == 0 ? basis : Hash64_CT(str + 1, n - 1, (basis ^ str[0]) * UINT64_C(1099511628211));
    }
}

template<size_t N> constexpr uint32_t CompileTimeHashString32(const char(&s)[N]) { return CompileTimeHashInternal::Hash32_CT(s, N - 1); }
template<size_t N> constexpr uint64_t CompileTimeHashString64(const char(&s)[N]) { return CompileTimeHashInternal::Hash64_CT(s, N - 1); }
