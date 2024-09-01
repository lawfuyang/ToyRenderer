#pragma once

// https://rigtorp.se/ringbuffer/
template <typename T, uint32_t N>
struct ConcurrentRingBuffer
{
    T m_Data[N];

#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
    alignas(64) std::atomic<size_t> m_ReadIdx{ 0 };
    alignas(64) size_t m_WriteIdxCached { 0 };
    alignas(64) std::atomic<size_t> m_WriteIdx{ 0 };
    alignas(64) size_t m_ReadIdxCached { 0 };
#pragma warning(pop)

    bool push(const T& val)
    {
        auto const writeIdx = m_WriteIdx.load(std::memory_order_relaxed);
        auto nextWriteIdx = writeIdx + 1;
        if (nextWriteIdx == N)
        {
            nextWriteIdx = 0;
        }
        if (nextWriteIdx == m_ReadIdxCached)
        {
            m_ReadIdxCached = m_ReadIdx.load(std::memory_order_acquire);
            if (nextWriteIdx == m_ReadIdxCached)
            {
                return false;
            }
        }
        m_Data[writeIdx] = val;
        m_WriteIdx.store(nextWriteIdx, std::memory_order_release);
        return true;
    }

    bool pop(T& val)
    {
        auto const readIdx = m_ReadIdx.load(std::memory_order_relaxed);
        if (readIdx == m_WriteIdxCached)
        {
            m_WriteIdxCached = m_WriteIdx.load(std::memory_order_acquire);
            if (readIdx == m_WriteIdxCached)
            {
                return false;
            }
        }
        val = m_Data[readIdx];
        auto nextReadIdx = readIdx + 1;
        if (nextReadIdx == N)
        {
            nextReadIdx = 0;
        }
        m_ReadIdx.store(nextReadIdx, std::memory_order_release);
        return true;
    }
};
