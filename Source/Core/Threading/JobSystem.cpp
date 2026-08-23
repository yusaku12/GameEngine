#include "Pch.h"

#include "Core\Threading\JobSystem.h"

namespace Engine
{
    //! 現在のスレッドがワーカースレッドかどうか
    static thread_local bool s_isWorkerThread = false;

    bool JobSystem::initialize(uint32_t workerCount)
    {
        if (m_running)
            return true;

        if (workerCount == 0)
        {
            const uint32_t concurrency = getHardwareConcurrency();
            workerCount = concurrency > 1 ? concurrency - 1 : 1;
        }

        m_running = true;
        m_workers.reserve(workerCount);

        for (uint32_t index = 0; index < workerCount; ++index)
            m_workers.emplace_back([this, index] { workerLoop(index); });

        LOG_INFO("[Job] ジョブシステムを初期化しました (ワーカー {} スレッド)", workerCount);
        return true;
    }

    void JobSystem::finalize()
    {
        if (!m_running)
            return;

        waitForAll();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }

        m_condition.notify_all();

        for (std::thread& worker : m_workers)
        {
            if (worker.joinable())
                worker.join();
        }

        m_workers.clear();
        m_jobs.clear();

        LOG_INFO("[Job] ジョブシステムを終了しました");
    }

    void JobSystem::schedule(JobFunction function, JobCounter* counter)
    {
        if (!function)
            return;

        if (counter != nullptr)
            counter->increment();

        m_pendingCount.fetch_add(1, std::memory_order_relaxed);

        if (!m_running)
        {
            // 初期化前・終了後は呼び出し元で同期的に実行する
            Job job{ std::move(function), counter };
            executeJob(job);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back(Job{ std::move(function), counter });
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
        const size_t suggested = (count + workerCount - 1) / workerCount;
        const size_t chunkSize = suggested > grainSize ? suggested : grainSize;

        JobCounter counter;

        for (size_t begin = 0; begin < count; begin += chunkSize)
        {
            const size_t end = begin + chunkSize < count ? begin + chunkSize : count;

            schedule([&body, begin, end]
                {
                    for (size_t index = begin; index < end; ++index)
                        body(index);
                }, &counter);
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
        setCurrentThreadName(spdlog::fmt_lib::format("EngineWorker{}", index).c_str());

        for (;;)
        {
            Job job;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this] { return !m_jobs.empty() || !m_running; });

                if (m_jobs.empty())
                {
                    if (!m_running)
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
        if (job.function)
            job.function();

        if (job.counter != nullptr)
            job.counter->decrement();

        m_pendingCount.fetch_sub(1, std::memory_order_release);
    }
} // namespace Engine