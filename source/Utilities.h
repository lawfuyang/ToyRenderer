#pragma once

#include "extern/magic_enum/magic_enum.hpp"
#include "extern/json/json.hpp"

#include "MathUtilities.h"

const char* StringFormat(const char* format, ...);

// In case you need to format huge strings > 1kb (but <1mb) in length
template<typename... Args>
inline const char* StringFormatBig(const char* format, Args&&... args)
{
    const uint32_t NbBuffers = 8;
    thread_local uint32_t bufferIdx = 0;
    thread_local std::string tl_Result[NbBuffers];

    bufferIdx = (bufferIdx + 1) % NbBuffers;

    const size_t size = snprintf(nullptr, 0, format, args...) + 1; // Extra space for '\0'
    tl_Result[bufferIdx].resize(size);
    snprintf(tl_Result[bufferIdx].data(), size, format, args ...);

    return tl_Result[bufferIdx].c_str();
}

// Returning the time in this format (yyyy_mm_dd_hh_mm_ss) allows for easy sorting of filenames
const char* GetTimeStamp();
const char* GetLastErrorAsString();
const char* GetExecutableDirectory();
const char* GetApplicationDirectory();
const char* GetResourceDirectory();
void GetFilesInDirectory(std::vector<std::string>& out, std::string_view directory, const char* extFilter = nullptr);
inline const char* GetFileNameFromPath(std::string_view fullPath) { return StringFormat("%s", std::filesystem::path{ fullPath }.filename().string().c_str()); }
inline const char* GetFileExtensionFromPath(std::string_view fullPath) { return StringFormat("%s", std::filesystem::path{ fullPath }.extension().string().c_str()); }
void ReadDataFromFile(std::string_view filename, std::vector<std::byte>& data);
void ReadTextFromFile(std::string_view filename, std::string& str);
void TokenizeLine(char* in, std::vector<const char*>& tokens);
bool IsFileExists(const char* fullfilename);
void ReadJSONfile(std::string_view filePath, const std::function<void(nlohmann::json)>& processDataFunctor);

struct WindowsHandleWrapper
{
    WindowsHandleWrapper(::HANDLE hdl = nullptr)
        : m_Handle(hdl)
    {}

    ~WindowsHandleWrapper()
    {
        ::FindClose(m_Handle);
        ::CloseHandle(m_Handle);
    }

    operator ::HANDLE() const { return m_Handle; }

    ::HANDLE m_Handle = nullptr;
};

struct CFileWrapper
{
    CFileWrapper(const char*fileName, bool isReadMode = true);
    ~CFileWrapper();

    CFileWrapper(const CFileWrapper&) = delete;
    CFileWrapper& operator=(const CFileWrapper&) = delete;

    operator bool() const { return m_File; }
    operator FILE* () const { return m_File; }

    FILE* m_File = nullptr;
};

template <typename Functor, bool ForwardScan = true>
static void RunOnAllBits(uint32_t mask, Functor&& func)
{
    std::bitset<sizeof(uint32_t) * CHAR_BIT> bitSet{ mask };
    unsigned long idx;
    auto ForwardScanFunc = [&]() { return _BitScanForward(&idx, bitSet.to_ulong()); };
    auto ReverseScanFunc = [&]() { return _BitScanReverse(&idx, bitSet.to_ulong()); };

    while (ForwardScan ? ForwardScanFunc() : ReverseScanFunc())
    {
        bitSet.set(idx, false);
        func(idx);
    }
}

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

    // In case you need to convert huge strings > 1kb in length
    const wchar_t* Utf8ToWideBig(std::string_view strView);
    const char* WideToUtf8Big(std::wstring_view strView);

    // TODO: wstrings
    inline void TransformStrInplace(std::string& str, int (*transformFunc)(int)) { std::transform(str.begin(), str.end(), str.begin(), [transformFunc](char c) { return static_cast<char>(transformFunc(static_cast<int>(c))); }); }
    inline void ToLower(std::string& str) { TransformStrInplace(str, std::tolower); }
    inline void ToUpper(std::string& str) { TransformStrInplace(str, std::toupper); }
}

float RandomFloat(float range = 1.0f);
uint32_t RandomUInt(uint32_t range = UINT_MAX);

inline const char* HRToString(HRESULT hr) { return StringFormat("HRESULT of 0x%08X", static_cast<UINT>(hr)); }

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

namespace crc
{
    //https://stackoverflow.com/questions/28675727/using-crc32-algorithm-to-hash-string-at-compile-time 
    template <uint64_t c, int32_t k = 8>
    struct CrcInternal : CrcInternal<((c & 1) ? 0xd800000000000000L : 0) ^ (c >> 1), k - 1>
    {};

    template <uint64_t c> struct CrcInternal<c, 0>
    {
        static constexpr uint64_t value = c;
    };

    template<uint64_t c>
    constexpr uint64_t CrcInternalValue = CrcInternal<c>::value;

    #define CRC64_TABLE_0(x) CRC64_TABLE_1(x) CRC64_TABLE_1(x + 128)
    #define CRC64_TABLE_1(x) CRC64_TABLE_2(x) CRC64_TABLE_2(x +  64)
    #define CRC64_TABLE_2(x) CRC64_TABLE_3(x) CRC64_TABLE_3(x +  32)
    #define CRC64_TABLE_3(x) CRC64_TABLE_4(x) CRC64_TABLE_4(x +  16)
    #define CRC64_TABLE_4(x) CRC64_TABLE_5(x) CRC64_TABLE_5(x +   8)
    #define CRC64_TABLE_5(x) CRC64_TABLE_6(x) CRC64_TABLE_6(x +   4)
    #define CRC64_TABLE_6(x) CRC64_TABLE_7(x) CRC64_TABLE_7(x +   2)
    #define CRC64_TABLE_7(x) CRC64_TABLE_8(x) CRC64_TABLE_8(x +   1)
    #define CRC64_TABLE_8(x) CrcInternalValue<x>,

    static constexpr uint64_t CRC_TABLE[] = { CRC64_TABLE_0(0) };

    constexpr uint64_t crc64_impl(const char* str, size_t N)
    {
        uint64_t val = 0xFFFFFFFFFFFFFFFFL;
        for (size_t idx = 0; idx < N; ++idx)
        {
            val = (val >> 8) ^ CRC_TABLE[(val ^ str[idx]) & 0xFF];
        }

        return val;
    }
}

// guaranteed compile time using consteval
template<size_t N>
consteval uint64_t crc64(char const (&_str)[N])
{
    return crc::crc64_impl(_str, N - 1);
}

inline uint64_t crc64(char const* _str, size_t N)
{
    return crc::crc64_impl(_str, N);
}

class Timer
{
public:
    static constexpr float DurationSecondRatio = std::chrono::seconds(1) * 1.0f / std::chrono::microseconds(1);
    static constexpr float DurationMsRatio = std::chrono::milliseconds(1) * 1.0f / std::chrono::microseconds(1);

    static float SecondsToMicroSeconds(float seconds) { return (float)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(seconds)).count(); }
    static float SecondsToMilliSeconds(float seconds) { return SecondsToMicroSeconds(seconds) / DurationMsRatio; }

    std::chrono::microseconds::rep GetElapsedMicroSeconds() const { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(); }
    float GetElapsedSeconds() const { return GetElapsedMicroSeconds() / DurationSecondRatio; }
    float GetElapsedMilliSeconds() const { return GetElapsedMicroSeconds() / DurationMsRatio; }

private:
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};

struct ScopedTimer
{
    ScopedTimer(std::string_view name)
    {
        assert(name.size() < sizeof(m_Name));
        strcpy_s(m_Name, sizeof(m_Name), name.data());
    }

    ~ScopedTimer();

    char m_Name[128];
    Timer m_Timer;
};
#define SCOPED_TIMER_NAMED(name) ScopedTimer GENERATE_UNIQUE_VARIABLE(scopedTimer){name};
#define SCOPED_TIMER_FUNCTION() SCOPED_TIMER_NAMED(__FUNCTION__);
