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
    const wchar_t* Utf8ToWide(std::string_view strView);
    const char* WideToUtf8(std::wstring_view strView);

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

class Timer
{
public:
    static constexpr float DurationSecondRatio = std::chrono::seconds(1) * 1.0f / std::chrono::microseconds(1);
    static constexpr float DurationMsRatio = std::chrono::milliseconds(1) * 1.0f / std::chrono::microseconds(1);

    static constexpr float SecondsToMicroSeconds(float seconds) { return (float)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(seconds)).count(); }
    static constexpr float SecondsToMilliSeconds(float seconds) { return SecondsToMicroSeconds(seconds) / DurationMsRatio; }

    std::chrono::microseconds::rep GetElapsedMicroSeconds() const { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(); }
    float GetElapsedSeconds() const { return GetElapsedMicroSeconds() / DurationSecondRatio; }
    float GetElapsedMilliSeconds() const { return GetElapsedMicroSeconds() / DurationMsRatio; }

private:
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
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
