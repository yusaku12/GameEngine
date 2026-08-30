#pragma once

namespace Engine
{
    /**
     * @brief 時間管理クラス
     * フレーム間の経過時間や平均フレームレートを算出する
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
         * @brief 計測を開始する
         */
        void initialize();

        /**
         * @brief 時間を更新する（毎フレーム先頭で呼び出す）
         */
        void update();

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール適用後）
         * @return float 経過時間（秒）
         */
        float getDeltaTime() const { return m_deltaTime * m_timeScale; }

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール未適用）
         * @return float 経過時間（秒）
         */
        float getUnscaledDeltaTime() const { return m_deltaTime; }

        /**
         * @brief 計測開始からの経過時間を取得する
         * @return float 経過時間（秒）
         */
        float getTotalTime() const { return m_totalTime; }

        /**
         * @brief 直近1秒間の平均フレームレートを取得する
         * @return float フレームレート
         */
        float getFrameRate() const { return m_frameRate; }

        /**
         * @brief 計測開始からのフレーム数を取得する
         * @return uint64_t フレーム数
         */
        uint64_t getFrameCount() const { return m_frameCount; }

        /**
         * @brief 時間の進み方を設定する
         * @param scale 倍率（1.0で等倍、0.0で停止）
         */
        void setTimeScale(float scale) { m_timeScale = maximum(0.0f, scale); }

        /**
         * @brief 時間の進み方を取得する
         * @return float 倍率
         */
        float getTimeScale() const { return m_timeScale; }

        /**
         * @brief 1フレームの経過時間の上限を設定する
         * ブレークポイントなどで長時間停止した際の跳ねを抑える
         * @param seconds 上限（秒）
         */
        void setMaxDeltaTime(float seconds) { m_maxDeltaTime = seconds; }

    private:

        using Clock = std::chrono::high_resolution_clock;

        TimeManager() = default;
        ~TimeManager() = default;

        GE_DISABLE_COPY_AND_MOVE(TimeManager);

        Clock::time_point m_startTime{};            //!< 計測を開始した時刻
        Clock::time_point m_previousTime{};         //!< 前フレームの時刻
        float             m_deltaTime = 0.0f;       //!< 前フレームからの経過時間
        float             m_totalTime = 0.0f;       //!< 計測開始からの経過時間
        float             m_timeScale = 1.0f;       //!< 時間の進み方
        float             m_maxDeltaTime = 0.25f;   //!< 1フレームの経過時間の上限
        float             m_frameRate = 0.0f;       //!< 直近の平均フレームレート
        float             m_frameRateTimer = 0.0f;  //!< 平均を取る区間の経過時間
        uint32_t          m_frameRateCount = 0;     //!< 平均を取る区間のフレーム数
        uint64_t          m_frameCount = 0;         //!< 計測開始からのフレーム数
    };
} // namespace Engine
