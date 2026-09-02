#include "Pch.h"
#include "HighResolutionTimer.h"

namespace Engine
{
    HighResolutionTimer::HighResolutionTimer()
    {
        LARGE_INTEGER freq;
        if (QueryPerformanceFrequency(&freq))
        {
            m_frequency = static_cast<uint64_t>(freq.QuadPart);
        }
        else
        {
            // フォールバック: 1秒 = 10,000,000ティック
            m_frequency = 10'000'000ULL;
        }

        reset();
    }

    void HighResolutionTimer::initialize()
    {
        reset();
    }

    void HighResolutionTimer::reset()
    {
        LARGE_INTEGER counter;
        if (QueryPerformanceCounter(&counter))
        {
            m_startCounter = counter.QuadPart;
        }
        else
        {
            m_startCounter = 0;
        }
    }

    double HighResolutionTimer::getElapsedSeconds() const
    {
        LARGE_INTEGER counter;
        if (!QueryPerformanceCounter(&counter))
        {
            return 0.0;
        }

        const int64_t elapsed = counter.QuadPart - m_startCounter;
        return static_cast<double>(elapsed) / static_cast<double>(m_frequency);
    }

    uint64_t HighResolutionTimer::getElapsedTicks() const
    {
        LARGE_INTEGER counter;
        if (!QueryPerformanceCounter(&counter))
        {
            return 0ULL;
        }

        const int64_t elapsed = counter.QuadPart - m_startCounter;
        return static_cast<uint64_t>(elapsed);
    }
} // namespace Engine