#pragma once

#include "Core\CoreDefines.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Engine
{
    /**
     * @brief Frame内で計測するCPU処理区間。
     */
    enum class FrameProfileMetric : std::uint8_t
    {
        GameUpdate,
        Physics,
        RenderUpdate,
        RenderQueue,
        DrawPreparation,
        CommandRecording,
        SubmitAndPresent,
        RenderFenceWait,
        Count,
    };

    /**
     * @brief 1 Frame分のCPU/GPU計測結果。
     */
    struct FrameProfileSample
    {
        std::uint64_t frameId = 0; //!< Frameを識別する連番
        double frameWallMilliseconds = 0.0; //!< 入力から描画完了までの経過時間。待機時間を含む
        std::array<double, static_cast<std::size_t>(FrameProfileMetric::Count)> cpuMilliseconds{}; //!< CPU区間時間
        double gpuMilliseconds = 0.0; //!< GPU Timestampで計測した描画コマンド実行時間
        bool hasGpuMeasurement = false; //!< GPU計測結果が利用可能か
    };

    /**
     * @brief 直近FrameのCPU/GPU計測履歴。
     */
    struct FrameProfilerSnapshot
    {
        static constexpr std::size_t HISTORY_SIZE = 120;

        std::array<FrameProfileSample, HISTORY_SIZE> samples{}; //!< 古いFrameから順に並んだ履歴
        std::size_t sampleCount = 0; //!< 有効な履歴数
    };

    /**
     * @brief CPU処理区間とFrameごとの計測履歴を管理する。
     * @thread_safety Thread-safe.
     */
    class FrameProfiler
    {
    public:

        /**
         * @brief CPU処理区間をスコープ終了時に記録する。
         */
        class ScopedCpuTimer
        {
        public:

            explicit ScopedCpuTimer(FrameProfileMetric metric) noexcept;
            ~ScopedCpuTimer() noexcept;

            GE_DISABLE_COPY_AND_MOVE(ScopedCpuTimer);

        private:

            FrameProfileMetric m_metric;
            std::uint64_t m_frameId = 0;
            std::chrono::steady_clock::time_point m_startTime;
        };

        /** @brief FrameProfilerのSingletonを取得する。 */
        static FrameProfiler& instance() noexcept;

        /**
         * @brief 新しいFrameの計測を開始する。
         * @return Frame識別子。既に計測中の場合は0。
         */
        std::uint64_t beginFrame() noexcept;

        /**
         * @brief Frame計測を完了して履歴へ追加する。
         * @param frameId beginFrameが返した識別子
         * @param wallMilliseconds Frame全体の壁時計時間
         * @return 登録に成功した場合はtrue
         */
        bool endFrame(std::uint64_t frameId, double wallMilliseconds) noexcept;

        /**
         * @brief GPU計測結果を対応する履歴Frameへ登録する。
         * @param frameId GPUクエリを記録したFrame識別子
         * @param gpuMilliseconds GPU Timestamp間の経過時間
         * @return 登録に成功した場合はtrue
         */
        bool recordGpuTime(std::uint64_t frameId, double gpuMilliseconds) noexcept;

        /** @brief 現在計測中のFrame識別子を取得する。 */
        std::uint64_t currentFrameId() const noexcept;

        /** @brief UI表示用に直近120 Frameの履歴を取得する。 */
        FrameProfilerSnapshot capture() const noexcept;

    private:

        FrameProfiler() = default;
        GE_DISABLE_COPY_AND_MOVE(FrameProfiler);

        void recordCpuTime(std::uint64_t frameId, FrameProfileMetric metric, double milliseconds) noexcept;

        mutable std::mutex m_mutex;
        FrameProfileSample m_currentSample{};
        std::array<FrameProfileSample, FrameProfilerSnapshot::HISTORY_SIZE> m_history{};
        std::uint64_t m_nextFrameId = 1;
        std::size_t m_nextHistoryIndex = 0;
        std::size_t m_historyCount = 0;
        bool m_frameActive = false;
    };
} // namespace Engine
