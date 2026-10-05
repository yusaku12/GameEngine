#include "Pch.h"
#include "Core\Threading\JobSystem.h"
#include "Core\Threading\ThreadDebugStats.h"

namespace Engine
{
    bool JobCounter::increment(const uint32_t count)
    {
        if (count == 0)
            return true;

        uint32_t current = m_value.load(std::memory_order_acquire);
        while (true)
        {
            if (count > (std::numeric_limits<uint32_t>::max)() - current)
            {
                LOG_ERROR("[Job] Cannot increment the job counter because it would overflow.");
                return false;
            }
            if (m_value.compare_exchange_weak(current, current + count,
                std::memory_order_acq_rel, std::memory_order_acquire))
                return true;
        }
    }

    bool JobCounter::decrement() noexcept
    {
        uint32_t current = m_value.load(std::memory_order_acquire);
        while (current != 0)
        {
            if (m_value.compare_exchange_weak(current, current - 1,
                std::memory_order_acq_rel, std::memory_order_acquire))
                return true;
        }
        LOG_ERROR("[Job] Cannot decrement a completed job counter.");
        return false;
    }

    JobDependency::JobDependency(uint32_t count)
    {
        m_remaining.store(count, std::memory_order_release);
    }

    bool JobDependency::addDependency(uint32_t count)
    {
        if (count == 0)
            return true;

        uint32_t remaining = m_remaining.load(std::memory_order_acquire);
        while (true)
        {
            if (count > (std::numeric_limits<uint32_t>::max)() - remaining)
            {
                LOG_ERROR("[Job] Cannot add dependencies because the dependency count would overflow.");
                return false;
            }
            if (m_remaining.compare_exchange_weak(remaining, remaining + count,
                std::memory_order_acq_rel, std::memory_order_acquire))
                return true;
        }
    }

    void JobDependency::complete(uint32_t count)
    {
        if (count == 0)
            return;

        uint32_t remaining = m_remaining.load(std::memory_order_acquire);
        while (remaining != 0)
        {
            const uint32_t updated = remaining > count ? remaining - count : 0;
            if (m_remaining.compare_exchange_weak(remaining, updated,
                std::memory_order_acq_rel, std::memory_order_acquire))
            {
                if (updated == 0)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_condition.notify_all();
                }
                return;
            }
        }
    }

    bool JobDependency::isReady() const
    {
        return m_remaining.load(std::memory_order_acquire) == 0;
    }

    void JobDependency::wait() const
    {
        if (isReady())
            return;

        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return m_remaining.load(std::memory_order_acquire) == 0; });
    }

    //! 現在のスレッドがワーカースレッドかどうか
    static thread_local bool s_isWorkerThread = false;

    bool JobSystem::initialize(uint32_t workerCount)
    {
        std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
        if (m_running.load(std::memory_order_acquire))
            return true;

        if (workerCount == 0)
        {
            const uint32_t concurrency = getHardwareConcurrency();
            workerCount = concurrency > 1 ? concurrency - 1 : 1;
        }

        try
        {
            m_workers.reserve(workerCount);
            m_running.store(true, std::memory_order_release);

            for (uint32_t index = 0; index < workerCount; ++index)
                m_workers.emplace_back([this, index] { workerLoop(index); });
        }
        catch (const std::exception& exception)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_running.store(false, std::memory_order_release);
            }
            m_condition.notify_all();
            waitForAll();
            for (std::thread& worker : m_workers)
            {
                if (worker.joinable())
                    worker.join();
            }
            m_workers.clear();
            ThreadDebugStats::instance().setWorkerCount(0);
            LOG_ERROR("[Job] ワーカースレッドの初期化に失敗しました: {}", exception.what());
            return false;
        }

        ThreadDebugStats::instance().setWorkerCount(workerCount);

        LOG_INFO("[Job] ジョブシステムを初期化しました (ワーカー {} スレッド)", workerCount);
        return true;
    }

    void JobSystem::finalize()
    {
        std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running.load(std::memory_order_acquire))
                return;

            m_running.store(false, std::memory_order_release);
        }
        m_condition.notify_all();
        waitForAll();

        for (std::thread& worker : m_workers)
        {
            if (worker.joinable())
                worker.join();
        }

        m_workers.clear();
        m_jobs.clear();
        ThreadDebugStats::instance().setWorkerCount(0);

        LOG_INFO("[Job] ジョブシステムを終了しました");
    }

    void JobSystem::schedule(JobFunction function, JobCounter* counter, const std::shared_ptr<CancellationToken>& cancellationToken)
    {
        if (!function)
            return;

        if (cancellationToken != nullptr && cancellationToken->isCancelled())
            return;

        Job job{ std::move(function), counter, cancellationToken, nullptr };
        bool executeInline = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running.load(std::memory_order_acquire))
            {
                m_jobs.push_back(std::move(job));
                // Commit accounting after a successful allocation and before releasing the shutdown lock.
                if (counter != nullptr && !counter->increment())
                {
                    m_jobs.pop_back();
                    return;
                }
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                // 初期化前・終了後は呼び出し元で同期的に実行する
                if (counter != nullptr && !counter->increment())
                    return;
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
                executeInline = true;
            }
        }

        if (executeInline)
        {
            executeJob(job);
            return;
        }
        m_condition.notify_one();
    }

    void JobSystem::scheduleWithDependency(JobFunction function, const std::shared_ptr<JobDependency>& dependency,
        JobCounter* counter, const std::shared_ptr<CancellationToken>& cancellationToken)
    {
        if (!function)
            return;

        if (cancellationToken != nullptr && cancellationToken->isCancelled())
            return;

        Job job{ std::move(function), counter, cancellationToken, dependency };
        bool executeInline = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running.load(std::memory_order_acquire))
            {
                m_jobs.push_back(std::move(job));
                if (counter != nullptr && !counter->increment())
                {
                    m_jobs.pop_back();
                    return;
                }
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                if (counter != nullptr && !counter->increment())
                    return;
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
                executeInline = true;
            }
        }

        if (executeInline)
        {
            executeJob(job);
            return;
        }
        m_condition.notify_one();
    }

    void JobSystem::scheduleWithContinuation(JobFunction function, Continuation continuation,
        JobCounter* counter, const std::shared_ptr<CancellationToken>& cancellationToken)
    {
        if (!function)
            return;

        if (cancellationToken != nullptr && cancellationToken->isCancelled())
            return;

        Job job{ std::move(function), counter, cancellationToken, nullptr };
        if (continuation.isValid())
        {
            job.function = [jobFunction = std::move(job.function), continuation]() mutable
                {
                    try
                    {
                        if (jobFunction)
                            jobFunction();
                    }
                    catch (...)
                    {
                        LOG_ERROR("[Job] 継続付きジョブの実行中に例外が発生しました");
                    }

                    continuation.run();
                };
        }

        bool executeInline = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running.load(std::memory_order_acquire))
            {
                m_jobs.push_back(std::move(job));
                if (counter != nullptr && !counter->increment())
                {
                    m_jobs.pop_back();
                    return;
                }
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                if (counter != nullptr && !counter->increment())
                    return;
                m_pendingCount.fetch_add(1, std::memory_order_relaxed);
                executeInline = true;
            }
        }

        if (executeInline)
        {
            executeJob(job);
            return;
        }
        m_condition.notify_one();
    }

    void JobSystem::parallelFor(size_t count, const std::function<void(size_t)>& body, size_t grainSize)
    {
        if (count == 0 || !body)
            return;

        if (grainSize == 0)
            grainSize = 1;

        // ワーカー数に対して細かすぎない粒度へ調整する
        const size_t workerCount = static_cast<size_t>(getWorkerCount()) + 1;
        const size_t suggested = count / workerCount + (count % workerCount != 0 ? 1 : 0);
        const size_t chunkSize = suggested > grainSize ? suggested : grainSize;

        JobCounter counter;

        for (size_t begin = 0; begin < count;)
        {
            const size_t remaining = count - begin;
            const size_t end = begin + (remaining < chunkSize ? remaining : chunkSize);

            schedule([&body, begin, end]
                {
                    for (size_t index = begin; index < end; ++index)
                        body(index);
                }, &counter);

            begin = end;
        }

        wait(counter);
    }

    void JobSystem::wait(JobCounter& counter)
    {
        while (!counter.isComplete())
        {
            Job job;
            if (tryPopJob(job))
                executeJob(job);
            else
                yieldThread();
        }
    }

    void JobSystem::waitForAll()
    {
        while (m_pendingCount.load(std::memory_order_acquire) != 0)
        {
            Job job;
            if (tryPopJob(job))
                executeJob(job);
            else
                yieldThread();
        }
    }

    bool JobSystem::isWorkerThread()
    {
        return s_isWorkerThread;
    }

    void JobSystem::workerLoop(uint32_t index)
    {
        s_isWorkerThread = true;
        setCurrentThreadRole(ThreadRole::Worker);
        setCurrentThreadName(spdlog::fmt_lib::format("EngineWorker{}", index).c_str());

        for (;;)
        {
            Job job;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this] { return !m_jobs.empty() || !m_running.load(std::memory_order_acquire); });

                if (m_jobs.empty())
                {
                    if (!m_running.load(std::memory_order_acquire))
                        break;

                    continue;
                }

                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }

            executeJob(job);
        }
    }

    bool JobSystem::tryPopJob(Job& outJob)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_jobs.empty())
            return false;

        outJob = std::move(m_jobs.front());
        m_jobs.pop_front();

        return true;
    }

    void JobSystem::executeJob(Job& job)
    {
        ThreadDebugStats::ScopedTask debugTask(ThreadDebugTask::JobExecution);

        while (job.dependency != nullptr
            && !job.dependency->isReady()
            && (job.cancellationToken == nullptr || !job.cancellationToken->isCancelled()))
        {
            Job pendingJob;
            if (tryPopJob(pendingJob))
                executeJob(pendingJob);
            else
                yieldThread();
        }

        if (job.cancellationToken != nullptr && job.cancellationToken->isCancelled())
        {
            if (job.counter != nullptr)
                job.counter->decrement();

            m_pendingCount.fetch_sub(1, std::memory_order_release);
            return;
        }

        try
        {
            if (job.function)
                job.function();
        }
        catch (...) {
            LOG_ERROR("[Job] ジョブ実行中に例外が発生しました");
        }

        if (job.counter != nullptr)
            job.counter->decrement();

        m_pendingCount.fetch_sub(1, std::memory_order_release);
    }
} // namespace Engine