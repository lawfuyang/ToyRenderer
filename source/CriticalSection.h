#pragma once

#include "extern/microprofile/microprofile.h"

// https://rigtorp.se/spinlock/
struct SpinLock {
    std::atomic<bool> lock_ = { 0 };

    void lock() noexcept {
        for (;;) {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange(true, std::memory_order_acquire)) {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load(std::memory_order_relaxed)) {
                // Issue X86 PAUSE to reduce contention between hyper-threads
                YieldProcessor();
            }
        }
    }

    void unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
};

#define PROFILE_LOCK(NAME) MICROPROFILE_SCOPEI("Locks", NAME, 0xFF0000)
#define AUTO_LOCK(lck) AUTO_SCOPE( [&]{ PROFILE_LOCK(TOSTRING(lck)); lck.lock(); } , [&]{ lck.unlock(); } )

class MultithreadDetector
{
public:
    void Enter(std::thread::id newID)
    {
        if (m_CurrentID != std::thread::id{} && newID != m_CurrentID)
            assert(false); // Multi-thread detected!
        m_CurrentID = newID;
    }

    void Exit() { m_CurrentID = std::thread::id{}; }

private:
    std::atomic<std::thread::id> m_CurrentID = {};
};

#define SCOPED_MULTITHREAD_DETECTOR(MTDetector) AUTO_SCOPE( [&]{ MTDetector.Enter(std::this_thread::get_id()); }, [&]{ MTDetector.Exit(); } );

#define STATIC_MULTITHREAD_DETECTOR() \
    static MultithreadDetector __s_MTDetector__; \
    SCOPED_MULTITHREAD_DETECTOR(__s_MTDetector__);
