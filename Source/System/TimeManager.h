#pragma once

/**
 * @brief 時間管理クラス
 *
 */
class TimeManager
{
public:

    /**
     * @brief インスタンスを取得する
     * @return TimeManager& インスタンス
     */
    static TimeManager& instance()
    {
        static TimeManager instance;
        return instance;
    }

    /**
     * @brief 時間の更新
     */
    void update();

private:

    TimeManager() = default;
    ~TimeManager() = default;

    TimeManager(const TimeManager&) = delete;
    TimeManager(TimeManager&&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    TimeManager& operator=(TimeManager&&) = delete;
};