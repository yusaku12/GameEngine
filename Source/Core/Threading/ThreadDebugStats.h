#pragma once

#include "Core\CoreDefines.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace Engine
{
    /**
     * @brief デバッグ表示で追跡するスレッド処理の種類。
     */
    enum class ThreadDebugTask : uint32_t
    {
        GameUpdate,
        RenderUpdate,
        JobExecution,
        Count,
    };

    /**
     * @brief 1種類のスレッド処理に対するデバッグ用スナップショット。
     */
    struct ThreadDebugTaskSnapshot
    {
        uint64_t totalRuns = 0;              //!< 実行回数
        uint32_t activeCount = 0;            //!< 現在実行中の数
        uint32_t lastThreadId = 0;           //!< 直近で実行したスレッドID
        uint64_t lastDurationMicroseconds = 0; //!< 直近の実行時間
        uint64_t totalDurationMicroseconds = 0; //!< 累積実行時間
    };

    /**
     * @brief スレッド処理全体のデバッグ用スナップショット。
     */
    struct ThreadDebugSnapshot
    {
        std::array<ThreadDebugTaskSnapshot, static_cast<size_t>(ThreadDebugTask::Count)> tasks{}; //!< 処理ごとの統計
        uint32_t workerCount = 0;          //!< JobSystemのワーカースレッド数
        uint32_t hardwareThreadCount = 0;  //!< ハードウェア並列数
    };

    /**
     * @brief スレッド処理の実行状況をデバッグ表示へ渡すための統計クラス。
     * @thread_safety Thread-safe.
     */
    class ThreadDebugStats
    {
    public:

        /**
         * @brief スコープ中の処理時間と実行スレッドを記録する。
         */
        class ScopedTask
        {
        public:
            explicit ScopedTask(ThreadDebugTask task) noexcept;
            ~ScopedTask() noexcept;

            GE_DISABLE_COPY_AND_MOVE(ScopedTask);

        private:
            ThreadDebugTask m_task; //!< 記録対象
            std::chrono::steady_clock::time_point m_startTime; //!< 開始時刻
        };

        /**
         * @brief インスタンスを取得する。
         * @return ThreadDebugStats& インスタンス
         */
        static ThreadDebugStats& instance()
        {
            static ThreadDebugStats instance;
            return instance;
        }

        /**
         * @brief ワーカー数を記録する。
         * @param workerCount JobSystemのワーカースレッド数
         */
        void setWorkerCount(uint32_t workerCount) noexcept;

        /**
         * @brief 現在の統計をUI表示用に取得する。
         * @return ThreadDebugSnapshot 統計のコピー
         */
        ThreadDebugSnapshot capture() const noexcept;

    private:

        struct TaskCounters
        {
            std::atomic<uint64_t> totalRuns{ 0 };
            std::atomic<uint32_t> activeCount{ 0 };
            std::atomic<uint32_t> lastThreadId{ 0 };
            std::atomic<uint64_t> lastDurationMicroseconds{ 0 };
            std::atomic<uint64_t> totalDurationMicroseconds{ 0 };
        };

        ThreadDebugStats() = default;

        GE_DISABLE_COPY_AND_MOVE(ThreadDebugStats);

        void beginTask(ThreadDebugTask task) noexcept;
        void endTask(ThreadDebugTask task, uint64_t durationMicroseconds) noexcept;

        static size_t taskIndex(ThreadDebugTask task) noexcept;

        std::array<TaskCounters, static_cast<size_t>(ThreadDebugTask::Count)> m_tasks{}; //!< 処理ごとの統計
        std::atomic<uint32_t> m_workerCount{ 0 }; //!< ワーカー数
    };
} // namespace Engine