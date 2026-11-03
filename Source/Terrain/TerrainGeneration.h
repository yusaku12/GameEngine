#pragma once

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include "Terrain\TerrainBuild.h"

namespace Engine
{
    /** @brief Gets the UI name of a generation stage. */
    const char* terrainGenerationStageName(TerrainGenerationStage stage) noexcept;
    /** @brief Read-only generation telemetry. Times are CPU wall milliseconds, not a frame-time guarantee. */
    struct TerrainGenerationStatus
    {
        TerrainGenerationStage stage = TerrainGenerationStage::Idle;
        double buildMilliseconds = 0;
        double applyMilliseconds = 0;
        std::uint32_t workerThread = 0;
        std::uint32_t applyThread = 0;
        /** @brief True while queued, building or awaiting frame-boundary application. */
        bool pending() const noexcept { return stage >= TerrainGenerationStage::Queued && stage <= TerrainGenerationStage::Ready; }
    };

    /**
     * @brief Shared task lifetime. The worker accesses only request/result and atomic state.
     * @details Completion callbacks run only from applyCompleted(); cancellation prevents callbacks.
     */
    class TerrainGenerationTask final
    {
    public:
        /** @brief Cancels without waiting; an in-progress CPU stage may finish, but its output cannot be applied. */
        void cancel() noexcept
        {
            const auto stage = m_stage.load(std::memory_order_acquire);
            if (stage >= TerrainGenerationStage::Queued && stage <= TerrainGenerationStage::Ready)
                m_cancelled.store(true, std::memory_order_release);
        }
        /** @brief Reads progress on the owner thread without waiting for the worker. */
        TerrainGenerationStatus status() const noexcept;
    private:
        friend class TerrainGenerationQueue;
        std::optional<TerrainBuildRequest> m_request;
        std::unique_ptr<TerrainBuildResult> m_result;
        std::function<bool(TerrainBuildResult&&)> m_apply;
        std::atomic<bool> m_cancelled{ false }, m_ready{ false };
        std::atomic<TerrainGenerationStage> m_stage{ TerrainGenerationStage::Queued };
        double m_buildMilliseconds = 0, m_applyMilliseconds = 0;
        std::uint32_t m_workerThread = 0, m_applyThread = 0;
    };

    /**
     * @brief One dedicated terrain worker shared across components; never executes on a frame/job waiter.
     * @thread_safety submit/applyCompleted/shutdown must run serially at an owner-thread frame barrier.
     * Workers never access live components, resource managers, GPU objects or PhysX actors.
     */
    class TerrainGenerationQueue final
    {
    public:
        /** @brief Gets the process-wide terrain build queue; worker starts lazily. */
        static TerrainGenerationQueue& instance();
        ~TerrainGenerationQueue();
        /**
         * @brief Queues an independent CPU build; logs and returns null on allocation/thread startup failure.
         * @param request Validated/resolved input snapshot.
         * @param apply Owner-thread callback, returning whether transactional application succeeded.
         * @return Cancellable task. Owner must cancel before any callback capture is destroyed.
         */
        std::shared_ptr<TerrainGenerationTask> submit(TerrainBuildRequest request,
            std::function<bool(TerrainBuildResult&&)> apply);
        /** @brief Applies at most one live result without waiting. Call only after game/render/physics work has joined. */
        void applyCompleted();
        /** @brief Cancels, joins and releases all work before PhysicsSystem shutdown; may be restarted by submit. */
        void shutdown();
    private:
        TerrainGenerationQueue() = default;
        void run();
        std::mutex m_mutex;
        std::condition_variable m_condition;
        std::vector<std::shared_ptr<TerrainGenerationTask>> m_queued;
        std::list<std::shared_ptr<TerrainGenerationTask>> m_tasks;
        std::thread m_worker;
        bool m_stopping = false;
        bool m_applying = false;
    };
}
