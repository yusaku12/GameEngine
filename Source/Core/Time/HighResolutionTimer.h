#pragma once

namespace Engine
{
    /**
     * @brief 高精度タイマー（Windows QueryPerformanceCounter使用）
     * Time Systemの基準タイマーとして、マイクロ秒単位の精度を提供する
     */
    class HighResolutionTimer
    {
    public:

        HighResolutionTimer();

        /**
         * @brief タイマーを初期化する
         */
        void initialize();

        /**
         * @brief タイマーをリセットする
         */
        void reset();

        /**
         * @brief リセット後の経過時間を秒単位で取得する
         * @return double 経過時間（秒、高精度）
         */
        double getElapsedSeconds() const;

        /**
         * @brief リセット後の経過時間をティック数で取得する
         * @return uint64_t 経過時間（ティック数）
         */
        uint64_t getElapsedTicks() const;

        /**
         * @brief タイマーの周波数（1秒あたりのティック数）を取得する
         * @return uint64_t 周波数
         */
        uint64_t getFrequency() const { return m_frequency; }

    private:

        uint64_t m_frequency = 0;      //!< タイマーの周波数（ティック/秒）
        int64_t  m_startCounter = 0;   //!< リセット時のカウンタ値
    };
} // namespace Engine
