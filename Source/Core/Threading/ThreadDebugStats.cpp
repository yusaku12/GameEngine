#include "Pch.h"
#include "Core\Threading\ThreadDebugStats.h"
#include "Core\Threading\ThreadUtility.h"

namespace Engine
{
    ThreadDebugStats::ScopedTask::ScopedTask(const ThreadDebugTask task) noexcept
        : m_task(task)
        , m_startTime(std::chrono::steady_clock::now())
    {
        ThreadDebugStats::instance().beginTask(m_task);
    }

    ThreadDebugStats::ScopedTask::~ScopedTask() noexcept
    {
        const auto endTime = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
        ThreadDebugStats::instance().endTask(m_task, static_cast<uint64_t>(duration.count()));
    }

    void ThreadDebugStats::setWorkerCount(const uint32_t workerCount) noexcept
    {
        m_workerCount.store(workerCount, std::memory_order_release);
    }

    ThreadDebugSnapshot ThreadDebugStats::capture() const noexcept
    {
        ThreadDebugSnapshot snapshot{};
        snapshot.workerCount = m_workerCount.load(std::memory_order_acquire);
        snapshot.hardwareThreadCount = getHardwareConcurrency();

        for (size_t index = 0; index < snapshot.tasks.size(); ++index)
        {
            const TaskCounters& counters = m_tasks[index];
            ThreadDebugTaskSnapshot& taskSnapshot = snapshot.tasks[index];
            taskSnapshot.totalRuns = counters.totalRuns.load(std::memory_order_acquire);
            taskSnapshot.activeCount = counters.activeCount.load(std::memory_order_acquire);
            taskSnapshot.lastThreadId = counters.lastThreadId.load(std::memory_order_acquire);
            taskSnapshot.lastDurationMicroseconds = counters.lastDurationMicroseconds.load(std::memory_order_acquire);
            taskSnapshot.totalDurationMicroseconds = counters.totalDurationMicroseconds.load(std::memory_order_acquire);
        }

        return snapshot;
    }

    void ThreadDebugStats::beginTask(const ThreadDebugTask task) noexcept
    {
        TaskCounters& counters = m_tasks[taskIndex(task)];
        counters.activeCount.fetch_add(1, std::memory_order_acq_rel);
        counters.lastThreadId.store(getCurrentThreadId(), std::memory_order_release);
    }

    void ThreadDebugStats::endTask(const ThreadDebugTask task, const uint64_t durationMicroseconds) noexcept
    {
        TaskCounters& counters = m_tasks[taskIndex(task)];
        counters.totalRuns.fetch_add(1, std::memory_order_acq_rel);
        counters.lastDurationMicroseconds.store(durationMicroseconds, std::memory_order_release);
        counters.totalDurationMicroseconds.fetch_add(durationMicroseconds, std::memory_order_acq_rel);
        counters.activeCount.fetch_sub(1, std::memory_order_acq_rel);
    }

    size_t ThreadDebugStats::taskIndex(const ThreadDebugTask task) noexcept
    {
        return static_cast<size_t>(task);
    }
} // namespace Engine