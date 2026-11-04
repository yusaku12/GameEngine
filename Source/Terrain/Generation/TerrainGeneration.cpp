#include "Pch.h"
#include "Terrain\Generation\TerrainGeneration.h"
#include "Core\Threading\ThreadUtility.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    const char* terrainGenerationStageName(const TerrainGenerationStage stage) noexcept
    {
        static constexpr std::array names{ "Idle", "Queued", "Height samples", "GPU noise", "GPU readback",
            "Hydraulic erosion", "Mesh", "Material weights",
            "Chunks", "PhysX cooking", "LOD", "Vegetation", "Ready to apply", "Applied", "Cancelled", "Failed" };
        const auto index = static_cast<std::size_t>(stage);
        return index < names.size() ? names[index] : "Invalid stage";
    }

    TerrainGenerationStatus TerrainGenerationTask::status() const noexcept
    {
        TerrainGenerationStatus value;
        const bool ready = m_ready.load(std::memory_order_acquire);
        value.stage = m_cancelled.load(std::memory_order_acquire) ? TerrainGenerationStage::Cancelled
            : m_stage.load(std::memory_order_acquire);
        if (!ready && value.stage == TerrainGenerationStage::Ready)
            value.stage = TerrainGenerationStage::Vegetation;
        if (ready)
        {
            value.buildMilliseconds = m_buildMilliseconds;
            value.workerThread = m_workerThread;
        }
        value.applyMilliseconds = m_applyMilliseconds;
        value.applyThread = m_applyThread;
        return value;
    }

    TerrainGenerationQueue& TerrainGenerationQueue::instance()
    {
        static TerrainGenerationQueue queue;
        return queue;
    }

    TerrainGenerationQueue::~TerrainGenerationQueue() { shutdown(); }

    std::shared_ptr<TerrainGenerationTask> TerrainGenerationQueue::submit(TerrainBuildRequest request,
        std::function<bool(TerrainBuildResult&&)> apply)
    {
        if (!apply || !validateTerrainBuildRequest(request))
        {
            LOG_ERROR("[TerrainGeneration] Invalid request or completion callback.");
            return nullptr;
        }
        try
        {
            m_tasks.remove_if([](const auto& task)
            {
                if (!task->m_cancelled.load(std::memory_order_acquire) || !task->m_ready.load(std::memory_order_acquire))
                    return false;
                task->m_request.reset();
                task->m_result.reset();
                task->m_apply = {};
                return true;
            });
            if (!m_worker.joinable())
            {
                m_stopping = false;
                m_worker = std::thread(&TerrainGenerationQueue::run, this);
            }
            auto task = std::make_shared<TerrainGenerationTask>();
            task->m_request.emplace(std::move(request));
            task->m_apply = std::move(apply);
            {
                const std::scoped_lock lock(m_mutex);
                std::erase_if(m_queued, [](const auto& queued)
                {
                    if (!queued->m_cancelled.load(std::memory_order_acquire))
                        return false;
                    queued->m_ready.store(true, std::memory_order_release);
                    return true;
                });
                // Finish all potentially throwing allocations before publishing to the worker.
                m_queued.reserve(m_queued.size() + 1);
                m_tasks.push_back(task);
                m_queued.push_back(task);
            }
            m_condition.notify_one();
            return task;
        }
        catch (const std::exception& exception)
        {
            LOG_ERROR("[TerrainGeneration] Cannot queue terrain build: {}.", exception.what());
            return nullptr;
        }
    }

    void TerrainGenerationQueue::run()
    {
        setCurrentThreadRole(ThreadRole::Background);
        setCurrentThreadName("TerrainGeneration");
        for (;;)
        {
            std::shared_ptr<TerrainGenerationTask> task;
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [this] { return m_stopping || !m_queued.empty(); });
                if (m_stopping)
                    return;
                task = std::move(m_queued.front());
                m_queued.erase(m_queued.begin());
            }
            if (task->m_cancelled.load(std::memory_order_acquire))
            {
                task->m_ready.store(true, std::memory_order_release);
                continue;
            }
            const auto start = std::chrono::steady_clock::now();
            task->m_workerThread = getCurrentThreadId();
            try
            {
                auto result = std::make_unique<TerrainBuildResult>();
                if (buildTerrain(std::move(*task->m_request), *result, &task->m_cancelled, &task->m_stage))
                    task->m_result = std::move(result);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR("[TerrainGeneration] Terrain build failed: {}.", exception.what());
            }
            catch (...)
            {
                LOG_ERROR("[TerrainGeneration] Terrain build failed with an unknown exception.");
            }
            task->m_buildMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            task->m_request.reset();
            task->m_stage.store(task->m_result ? TerrainGenerationStage::Ready : TerrainGenerationStage::Failed,
                std::memory_order_release);
            task->m_ready.store(true, std::memory_order_release);
        }
    }

    void TerrainGenerationQueue::applyCompleted()
    {
        if (m_applying)
        {
            LOG_ERROR("[TerrainGeneration] Reentrant completion processing is not permitted.");
            return;
        }
        m_applying = true;
        struct Scope { bool& applying; ~Scope() { applying = false; } } scope{ m_applying };
        std::list<std::shared_ptr<TerrainGenerationTask>> tasks;
        tasks.splice(tasks.end(), m_tasks);
        bool appliedResult = false;
        while (!tasks.empty())
        {
            const auto task = tasks.front();
            if (!task->m_ready.load(std::memory_order_acquire))
            {
                m_tasks.splice(m_tasks.end(), tasks, tasks.begin());
                continue;
            }
            if (task->m_cancelled.load(std::memory_order_acquire))
            {
                task->m_apply = {};
                task->m_request.reset();
                task->m_result.reset();
                tasks.pop_front();
                continue;
            }
            if (task->m_result && appliedResult)
            {
                m_tasks.splice(m_tasks.end(), tasks, tasks.begin());
                continue;
            }
            const auto start = std::chrono::steady_clock::now();
            bool applied = false;
            tasks.pop_front();
            if (task->m_result)
            {
                appliedResult = true;
                try { applied = task->m_apply(std::move(*task->m_result)); }
                catch (const std::exception& exception)
                {
                    LOG_ERROR("[TerrainGeneration] Terrain application failed: {}.", exception.what());
                }
                catch (...)
                {
                    LOG_ERROR("[TerrainGeneration] Terrain application failed with an unknown exception.");
                }
            }
            task->m_result.reset();
            task->m_apply = {};
            task->m_applyThread = getCurrentThreadId();
            task->m_applyMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            task->m_stage.store(applied ? TerrainGenerationStage::Applied : TerrainGenerationStage::Failed,
                std::memory_order_release);
        }
    }

    void TerrainGenerationQueue::shutdown()
    {
        if (m_applying)
        {
            LOG_ERROR("[TerrainGeneration] Cannot shut down from a completion callback.");
            return;
        }
        for (const auto& task : m_tasks)
            task->cancel();
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_condition.notify_all();
        if (m_worker.joinable())
            m_worker.join();
        for (const auto& task : m_tasks)
        {
            task->m_request.reset();
            task->m_result.reset();
            task->m_apply = {};
        }
        m_queued.clear();
        m_tasks.clear();
    }
}
