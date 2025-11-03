#pragma once

#include "magic_enum/magic_enum.hpp"

const char* StringFormat(const char* format, ...);

const char* GetExecutableDirectory();
const char* GetRootDirectory();
const char* GetApplicationDirectory();
void GetFilesInDirectory(std::vector<std::string>& out, std::string_view directory, const char* extFilter = nullptr);
inline const char* GetFileNameFromPath(std::string_view fullPath) { return StringFormat("%s", std::filesystem::path{ fullPath }.filename().string().c_str()); }
inline const char* GetFileExtensionFromPath(std::string_view fullPath) { return StringFormat("%s", std::filesystem::path{ fullPath }.extension().string().c_str()); }
void ReadDataFromFile(std::string_view filename, std::vector<std::byte>& data);
void ReadTextFromFile(std::string_view filename, std::string& str);
void TokenizeLine(char* in, std::vector<const char*>& tokens);

namespace EnumUtils
{
    template <typename EnumT> constexpr EnumT ToEnum(std::string_view s) { return magic_enum::enum_cast<EnumT>(s).value(); }
    template <typename EnumT> constexpr const char* ToString(EnumT e) { return magic_enum::enum_name(e).data(); }
    template <typename EnumT> constexpr uint32_t Count() { return (uint32_t)magic_enum::enum_count<EnumT>(); }
}

namespace StringUtils
{
    inline const wchar_t* Utf8ToWide(std::string_view strView)
    {
        return SDL_reinterpret_cast(const wchar_t*, (SDL_iconv_string("WCHAR_T", "UTF-8", strView.data(), (strView.length() + 1))));
    }

    inline const char* WideToUtf8(std::wstring_view strView)
    {
        return SDL_iconv_wchar_utf8(strView.data());
    }

    // TODO: wstrings
    inline void TransformStrInplace(std::string& str, int (*transformFunc)(int)) { std::transform(str.begin(), str.end(), str.begin(), [transformFunc](char c) { return static_cast<char>(transformFunc(static_cast<int>(c))); }); }
    inline void ToLower(std::string& str) { TransformStrInplace(str, std::tolower); }
    inline void ToUpper(std::string& str) { TransformStrInplace(str, std::toupper); }
}

float RandomFloat(float min = 0.0f, float max = 1.0f);
int32_t RandomInt(int32_t min = 0, int32_t max = INT32_MAX);
uint32_t RandomUInt(uint32_t min = 0, uint32_t max = UINT32_MAX);

template <class T>
inline void HashCombine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

inline std::size_t HashRange(std::byte* startByte, std::size_t nbBytes)
{
    std::size_t hash = 0;
    for (std::size_t i = 0; i < nbBytes; ++i)
    {
        HashCombine(hash, *(startByte + i));
    }
    return hash;
}

template <typename T>
inline std::size_t HashRawMem(const T& s)
{
    static_assert(std::is_standard_layout_v<T>);
    return HashRange((std::byte*)& s, sizeof(T));
}

struct ScopedFile
{
    ScopedFile(std::string_view filePath, const char* mode);
    ~ScopedFile();

    operator FILE*() const { return m_File; }
    FILE* m_File;
};

class Timer
{
public:
    static constexpr float DurationSecondRatio = std::chrono::seconds(1) * 1.0f / std::chrono::microseconds(1);
    static constexpr float DurationMsRatio = std::chrono::milliseconds(1) * 1.0f / std::chrono::microseconds(1);

    static constexpr float SecondsToMilliSeconds(float seconds) { return seconds * SDL_MS_PER_SECOND; }

    void Reset() { m_StartTick = SDL_GetTicksNS(); }

    uint64_t GetElapsedNanoseconds() const { return  SDL_GetTicksNS() - m_StartTick; }
    float GetElapsedMicroSeconds() const { return SDL_NS_TO_US((double)GetElapsedNanoseconds()); }
    float GetElapsedMilliseconds() const { return SDL_NS_TO_MS((double)GetElapsedNanoseconds()); }
    float GetElapsedSeconds() const { return SDL_NS_TO_SECONDS((double)GetElapsedNanoseconds()); }

private:
    uint64_t m_StartTick = SDL_GetTicksNS();
};

struct ScopedTimer
{
    ScopedTimer(std::string_view name)
        : m_Name(name.data())
    {}

    ~ScopedTimer();

    const char* m_Name;
    Timer m_Timer;
};
#define SCOPED_TIMER_NAMED(name) ScopedTimer GENERATE_UNIQUE_VARIABLE(scopedTimer){name};
#define SCOPED_TIMER_FUNCTION() SCOPED_TIMER_NAMED(__FUNCTION__);
