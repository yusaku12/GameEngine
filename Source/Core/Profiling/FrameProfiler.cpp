#include "Pch.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Logging\Logging.h"

namespace Engine
{
    FrameProfiler::ScopedCpuTimer::ScopedCpuTimer(const FrameProfileMetric metric) noexcept
        : m_metric(metric)
        , m_frameId(FrameProfiler::instance().currentFrameId())
        , m_startTime(std::chrono::steady_clock::now())
    {
    }

    FrameProfiler::ScopedCpuTimer::~ScopedCpuTimer() noexcept
    {
        if (m_frameId == 0)
            return;

        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_startTime);
        FrameProfiler::instance().recordCpuTime(m_frameId, m_metric, elapsed.count());
    }

    FrameProfiler& FrameProfiler::instance() noexcept
    {
        static FrameProfiler profiler;
        return profiler;
    }

    std::uint64_t FrameProfiler::beginFrame() noexcept
    {
        const std::lock_guard lock(m_mutex);
        if (m_frameActive)
        {
            LOG_ERROR("[Profiler] Cannot begin a Frame while another Frame is being measured.");
            return 0;
        }

        m_currentSample = {};
        m_currentSample.frameId = m_nextFrameId++;
        if (m_nextFrameId == 0)
            m_nextFrameId = 1;
        m_frameActive = true;
        return m_currentSample.frameId;
    }

    bool FrameProfiler::endFrame(const std::uint64_t frameId, const double wallMilliseconds) noexcept
    {
        if (!std::isfinite(wallMilliseconds) || wallMilliseconds < 0.0)
        {
            LOG_ERROR("[Profiler] Cannot record an invalid Frame duration.");
            return false;
        }

        const std::lock_guard lock(m_mutex);
        if (!m_frameActive || frameId == 0 || m_currentSample.frameId != frameId)
        {
            LOG_ERROR("[Profiler] Cannot end an unknown or inactive Frame.");
            return false;
        }

        m_currentSample.frameWallMilliseconds = wallMilliseconds;
        m_history[m_nextHistoryIndex] = m_currentSample;
        m_nextHistoryIndex = (m_nextHistoryIndex + 1) % m_history.size();
        m_historyCount = std::min(m_historyCount + 1, m_history.size());
        m_frameActive = false;
        return true;
    }

    bool FrameProfiler::recordGpuTime(const std::uint64_t frameId, const double gpuMilliseconds) noexcept
    {
        if (frameId == 0 || !std::isfinite(gpuMilliseconds) || gpuMilliseconds < 0.0)
        {
            LOG_ERROR("[Profiler] Cannot record an invalid GPU duration or Frame identifier.");
            return false;
        }

        const std::lock_guard lock(m_mutex);
        for (std::size_t offset = 0; offset < m_historyCount; ++offset)
        {
            const std::size_t index = (m_nextHistoryIndex + m_history.size() - 1 - offset) % m_history.size();
            FrameProfileSample& sample = m_history[index];
            if (sample.frameId != frameId)
                continue;

            sample.gpuMilliseconds = gpuMilliseconds;
            sample.hasGpuMeasurement = true;
            return true;
        }

        LOG_ERROR("[Profiler] GPU timing refers to a Frame that is no longer in the history.");
        return false;
    }

    std::uint64_t FrameProfiler::currentFrameId() const noexcept
    {
        const std::lock_guard lock(m_mutex);
        return m_frameActive ? m_currentSample.frameId : 0;
    }

    FrameProfilerSnapshot FrameProfiler::capture() const noexcept
    {
        const std::lock_guard lock(m_mutex);
        FrameProfilerSnapshot snapshot{};
        snapshot.sampleCount = m_historyCount;
        const std::size_t firstIndex = (m_nextHistoryIndex + m_history.size() - m_historyCount)
            % m_history.size();
        for (std::size_t index = 0; index < m_historyCount; ++index)
            snapshot.samples[index] = m_history[(firstIndex + index) % m_history.size()];
        return snapshot;
    }

    void FrameProfiler::recordCpuTime(
        const std::uint64_t frameId,
        const FrameProfileMetric metric,
        const double milliseconds) noexcept
    {
        const std::size_t metricIndex = static_cast<std::size_t>(metric);
        if (metricIndex >= static_cast<std::size_t>(FrameProfileMetric::Count)
            || !std::isfinite(milliseconds) || milliseconds < 0.0)
        {
            LOG_ERROR("[Profiler] Cannot record an invalid CPU metric or duration.");
            return;
        }

        const std::lock_guard lock(m_mutex);
        if (!m_frameActive || m_currentSample.frameId != frameId)
        {
            LOG_ERROR("[Profiler] CPU timing completed outside its Frame.");
            return;
        }
        m_currentSample.cpuMilliseconds[metricIndex] += milliseconds;
    }
} // namespace Engine
