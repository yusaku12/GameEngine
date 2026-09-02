#include "Pch.h"
#include "TimeManager.h"

namespace Engine
{
    TimeManager::TimeManager()
    {
        // m_smoothDeltaTimeBuffer は std::array として初期化される
    }

    void TimeManager::initialize()
    {
        m_timer.initialize();

        m_deltaTime = 0.0;
        m_unscaledDeltaTime = 0.0;
        m_gameTime = 0.0;
        m_unscaledTime = 0.0;
        m_timeScale = 1.0;

        m_fixedAccumulator = 0.0;

        m_frameCount = 0;
        m_fixedFrameCount = 0;

        m_fps = 0.0;
        m_averageFps = 0.0;
        m_fpsTimer = 0.0;
        m_fpsFrameCount = 0;

        m_smoothDeltaTime = 0.0;
        m_smoothBufferIndex = 0;
        std::fill(m_smoothDeltaTimeBuffer.begin(), m_smoothDeltaTimeBuffer.end(), 0.0);

        m_paused = false;

        LOG_DEBUG_CAT("Time", "[TimeManager] Initialized");
    }

    void TimeManager::beginFrame()
    {
        // 現在の経過時間を取得
        const double rawDeltaTime = m_timer.getElapsedSeconds();
        m_timer.reset();

        // Delta Time を Clamp
        const double clampedDeltaTime = minimum(rawDeltaTime, m_maxDeltaTime);

        // Unscaled Delta Time を更新
        if (m_paused)
        {
            m_unscaledDeltaTime = 0.0;
        }
        else
        {
            m_unscaledDeltaTime = clampedDeltaTime;
        }

        // Time Scale を適用して Delta Time を計算
        m_deltaTime = m_unscaledDeltaTime * m_timeScale;

        // ゲーム時間を更新
        m_gameTime += m_deltaTime;
        m_unscaledTime += m_unscaledDeltaTime;

        // Fixed Update Accumulator を更新
        m_fixedAccumulator += m_deltaTime;

        // フレームカウントを増加
        ++m_frameCount;

        // Smooth Delta Time を更新
        updateSmoothDeltaTime();

        // FPS統計を更新
        updateFpsStats();
    }

    void TimeManager::endFrame()
    {
        // 現在は処理なし
        // 将来的な拡張に備える
    }

    void TimeManager::setTimeScale(float scale) noexcept
    {
        double timeScale = static_cast<double>(scale);
        timeScale = maximum(0.0, timeScale);

        // NaN/Infinity チェック
        if (!std::isfinite(timeScale))
        {
            LOG_WARNING("[TimeManager] Invalid timeScale: {}, setting to 1.0", scale);
            timeScale = 1.0;
        }

        m_timeScale = timeScale;
    }

    void TimeManager::setFixedDeltaTime(float deltaTime) noexcept
    {
        double fixedDeltaTime = static_cast<double>(deltaTime);

        // 0以下の値をチェック
        if (fixedDeltaTime <= 0.0 || !std::isfinite(fixedDeltaTime))
        {
            LOG_WARNING("[TimeManager] Invalid fixedDeltaTime: {}, keeping previous value {}", deltaTime, m_fixedDeltaTime);
            return;
        }

        m_fixedDeltaTime = fixedDeltaTime;
    }

    void TimeManager::consumeFixedUpdate() noexcept
    {
        m_fixedAccumulator -= m_fixedDeltaTime;
        ++m_fixedFrameCount;
    }

    double TimeManager::realtimeSinceStartup() const noexcept
    {
        // 実時間はタイマーからの累積時間
        return m_unscaledTime + m_timer.getElapsedSeconds();
    }

    void TimeManager::updateSmoothDeltaTime() noexcept
    {
        // リングバッファに現在の Delta Time を格納
        m_smoothDeltaTimeBuffer[m_smoothBufferIndex] = m_unscaledDeltaTime;
        m_smoothBufferIndex = (m_smoothBufferIndex + 1) % SMOOTH_SAMPLE_COUNT;

        // バッファの平均を計算
        double sum = 0.0;
        for (const double value : m_smoothDeltaTimeBuffer)
        {
            sum += value;
        }

        m_smoothDeltaTime = sum / static_cast<double>(SMOOTH_SAMPLE_COUNT);
    }

    void TimeManager::updateFpsStats() noexcept
    {
        m_fpsTimer += m_unscaledDeltaTime;
        ++m_fpsFrameCount;

        // 瞬間的なFPS
        if (m_unscaledDeltaTime > 0.0)
        {
            m_fps = 1.0 / m_unscaledDeltaTime;
        }

        // 平均FPS を更新
        if (m_fpsTimer >= FPS_UPDATE_INTERVAL)
        {
            m_averageFps = static_cast<double>(m_fpsFrameCount) / m_fpsTimer;
            m_fpsTimer = 0.0;
            m_fpsFrameCount = 0;
        }
    }
} // namespace Engine