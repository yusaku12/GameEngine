#include "Pch.h"
#include "HighResolutionTimer.h"

namespace Engine
{
    HighResolutionTimer::HighResolutionTimer()
    {
        LARGE_INTEGER freq;
        if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
        {
            m_frequency = static_cast<uint64_t>(freq.QuadPart);
        }
        else
        {
            LOG_ERROR("[HighResolutionTimer] Failed to query performance counter frequency; using steady_clock.");
            m_usesSteadyClock = true;
            m_frequency = 1'000'000'000ULL;
        }

        reset();
    }

    void HighResolutionTimer::initialize()
    {
        reset();
    }

    void HighResolutionTimer::reset()
    {
        if (m_usesSteadyClock)
        {
            m_startCounter = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return;
        }

        LARGE_INTEGER counter;
        if (QueryPerformanceCounter(&counter))
        {
            m_startCounter = counter.QuadPart;
        }
        else
        {
            LOG_ERROR("[HighResolutionTimer] Failed to reset performance counter; switching to steady_clock.");
            m_usesSteadyClock = true;
            m_frequency = 1'000'000'000ULL;
            m_startCounter = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    double HighResolutionTimer::getElapsedSeconds() const
    {
        if (m_usesSteadyClock)
        {
            const int64_t currentCounter = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const int64_t elapsed = currentCounter - m_startCounter;
            return elapsed > 0 ? static_cast<double>(elapsed) / 1'000'000'000.0 : 0.0;
        }

        LARGE_INTEGER counter;
        if (!QueryPerformanceCounter(&counter))
        {
            LOG_ERROR("[HighResolutionTimer] Failed to query performance counter for elapsed seconds.");
            return 0.0;
        }

        const int64_t elapsed = counter.QuadPart - m_startCounter;
        if (elapsed <= 0)
            return 0.0;
        return static_cast<double>(elapsed) / static_cast<double>(m_frequency);
    }

    uint64_t HighResolutionTimer::getElapsedTicks() const
    {
        if (m_usesSteadyClock)
        {
            const int64_t currentCounter = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const int64_t elapsed = currentCounter - m_startCounter;
            return elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0ULL;
        }

        LARGE_INTEGER counter;
        if (!QueryPerformanceCounter(&counter))
        {
            LOG_ERROR("[HighResolutionTimer] Failed to query performance counter for elapsed ticks.");
            return 0ULL;
        }

        const int64_t elapsed = counter.QuadPart - m_startCounter;
        if (elapsed <= 0)
            return 0ULL;
        return static_cast<uint64_t>(elapsed);
    }
} // namespace Engine