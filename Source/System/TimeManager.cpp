#include "Pch.h"

#include "System\TimeManager.h"

namespace Engine
{

//! フレームレートを平均する区間の長さ（秒）
static constexpr float FRAME_RATE_INTERVAL = 0.5f;

void TimeManager::initialize()
{
    m_startTime = Clock::now();
    m_previousTime = m_startTime;

    m_deltaTime = 0.0f;
    m_totalTime = 0.0f;
    m_frameRate = 0.0f;
    m_frameRateTimer = 0.0f;
    m_frameRateCount = 0;
    m_frameCount = 0;
}

void TimeManager::update()
{
    const Clock::time_point now = Clock::now();
    const std::chrono::duration<float> elapsed = now - m_previousTime;

    m_previousTime = now;
    m_deltaTime = minimum(elapsed.count(), m_maxDeltaTime);
    m_totalTime = std::chrono::duration<float>(now - m_startTime).count();
    ++m_frameCount;

    m_frameRateTimer += m_deltaTime;
    ++m_frameRateCount;

    if (m_frameRateTimer >= FRAME_RATE_INTERVAL)
    {
        m_frameRate = static_cast<float>(m_frameRateCount) / m_frameRateTimer;
        m_frameRateTimer = 0.0f;
        m_frameRateCount = 0;
    }
}

} // namespace Engine
