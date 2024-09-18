#include "Utilities.h"

#include "Engine.h"

const char* StringFormat(const char* format, ...)
{
    const uint32_t kBufferSize = 256;
    const uint32_t kNbBuffers = 16;
    thread_local uint32_t bufferIdx = 0;
    thread_local char buffer[kBufferSize][kNbBuffers]{};

    bufferIdx = (bufferIdx + 1) % kNbBuffers;

    va_list marker;
    va_start(marker, format);
    const int result = _vsnprintf_s(buffer[bufferIdx], kBufferSize, _TRUNCATE, format, marker);
    va_end(marker);

    assert(result >= 0);

    return buffer[bufferIdx];
}

const char* GetExecutableDirectory()
{
    static std::string path = []
        {
            char buffer[512];
            GetModuleFileNameA(NULL, buffer, sizeof(buffer));

            std::filesystem::path path = buffer;
            return path.parent_path().string();
        }();
    return path.c_str();
}

const char* GetApplicationDirectory()
{
    static std::string s = std::filesystem::current_path().string();
    return s.c_str();
}

const char* GetResourceDirectory()
{
    static std::string s = (std::filesystem::path{ GetApplicationDirectory() }.parent_path() / "resources").string();
    return s.c_str();
}

void GetFilesInDirectory(std::vector<std::string>& out, std::string_view directory, const char* extFilter)
{
    for (const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator{ directory })
    {
        std::filesystem::path filePath = dir_entry.path();

        if (extFilter)
        {
            std::string_view extFilterStrView = extFilter;
            if (filePath.extension().string() != extFilterStrView)
            {
                continue;
            }
        }

        out.push_back(filePath.make_preferred().string());
    }
}

void ReadDataFromFile(std::string_view filename, std::vector<std::byte>& data)
{
    std::ifstream ifs{ filename.data(), std::ios::binary | std::ios::ate};
    if (!ifs)
        return;

    const std::streampos fileSize = ifs.tellg();
    data.resize(fileSize);

    ifs.seekg(0, std::ios::beg);
    ifs.read((char*)data.data(), fileSize);
}

void ReadTextFromFile(std::string_view dirFull, std::string& str)
{
    if (std::ifstream ifs{ dirFull.data() })
    {
        new (&str) std::string{ std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>() };
    }
}

void TokenizeLine(char* in, std::vector<const char*>& tokens)
{
    char* out = in;
    char* token = out;

    // Some magic to correctly tokenize spaces in ""
    bool isString = false;
    while (*in)
    {
        if (*in == '"')
            isString = !isString;
        else if (*in == ' ' && !isString)
        {
            *in = '\0';
            if (*token)
                tokens.push_back(token);
            token = out + 1;
        }

        if (*in != '"')
            *out++ = *in;

        in++;
    }
    *out = '\0';

    if (*token)
        tokens.push_back(token);
}

namespace StringUtils
{
    const wchar_t* Utf8ToWide(std::string_view strView)
    {
        thread_local std::wstring result;
        result.resize(strView.size());

        if (!MultiByteToWideChar(CP_ACP, 0, strView.data(), -1, result.data(), MAX_PATH))
            result[0] = L'\0';
        return result.c_str();
    }

    const char* WideToUtf8(std::wstring_view strView)
    {
        thread_local std::string result;
        result.resize(strView.size());

        if (!WideCharToMultiByte(CP_ACP, 0, strView.data(), -1, result.data(), MAX_PATH, nullptr, nullptr))
            result[0] = L'\0';
        return result.c_str();
    }
}

class RandomNumberGenerator
{
public:
    RandomNumberGenerator(uint32_t s = 0) : m_gen(m_rd())
    {
        if (s != 0)
            SetSeed(s);
    }

    // Default int range is [MIN_UINT, MAX_UINT].  Max value is included.
    uint32_t NextUInt(void)
    {
        return std::uniform_int_distribution<uint32_t>(0, UINT_MAX)(m_gen);
    }

    uint32_t NextUInt(uint32_t MaxVal)
    {
        return std::uniform_int_distribution<uint32_t>(0, MaxVal)(m_gen);
    }

    uint32_t NextUInt(uint32_t MinVal, uint32_t MaxVal)
    {
        return std::uniform_int_distribution<uint32_t>(MinVal, MaxVal)(m_gen);
    }

    // Default float range is [0.0f, 1.0f).  Max value is excluded.
    float NextFloat(float MaxVal = 1.0f)
    {
        return std::uniform_real_distribution<float>(0.0f, MaxVal)(m_gen);
    }

    float NextFloat(float MinVal, float MaxVal)
    {
        return std::uniform_real_distribution<float>(MinVal, MaxVal)(m_gen);
    }

    void SetSeed(uint32_t s)
    {
        m_gen.seed(s);
    }

private:
    std::random_device m_rd;
    std::minstd_rand m_gen;
};
static RandomNumberGenerator g_RandomNumberGenerator;

float RandomFloat(float range)
{
    return g_RandomNumberGenerator.NextFloat(range);
}

uint32_t RandomUInt(uint32_t range)
{
    return g_RandomNumberGenerator.NextUInt(range);
}

ScopedTimer::~ScopedTimer()
{
    LOG_DEBUG("ScopedTimer: [%s] took %f seconds", m_Name, m_Timer.GetElapsedSeconds());
}
