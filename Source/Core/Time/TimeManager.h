#pragma once

#include "HighResolutionTimer.h"

namespace Engine
{
    /**
     * @brief 時間管理クラス
     * フレーム間の経過時間、ゲーム時間、Fixed Update、Time Scale、Pauseなど、
     * ゲームエンジンに必要な包括的な時間管理機能を提供する。
     * Unity の Time クラスを参考に設計されている。
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
         * @brief フレーム開始時に呼び出す（BeginFrame）
         * 高精度タイマーから現在時刻を取得し、Delta Timeを計算する
         */
        void beginFrame();

        /**
         * @brief フレーム終了時に呼び出す（EndFrame）
         * 現在はプレースホルダー。将来の拡張に備える
         */
        void endFrame();

        /**
         * @brief 時間を更新する（互換性維持）
         * 従来の update() は beginFrame() のエイリアス
         */
        void update() { beginFrame(); }

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール適用後、float）
         * @details raw経過時間をmaxDeltaTimeで上限処理し、一時停止中は0にした値に
         * timeScaleを乗じた値。ゲーム更新およびゲーム時間の進行に使用する。
         * @return float 経過時間（秒）
         */
        float deltaTime() const noexcept { return static_cast<float>(m_deltaTime); }

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール適用後、高精度）
         * @return double 経過時間（秒）
         */
        double deltaTimePrecise() const noexcept { return m_deltaTime; }

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール未適用、float）
         * @details raw経過時間をmaxDeltaTimeで上限処理した値。一時停止中は0となる。
         * timeScaleが0でも、一時停止中でなければこの値は進行する。
         * @return float 経過時間（秒）
         */
        float unscaledDeltaTime() const noexcept { return static_cast<float>(m_unscaledDeltaTime); }

        /**
         * @brief 前フレームからの経過時間を取得する（タイムスケール未適用、高精度）
         * @return double 経過時間（秒）
         */
        double unscaledDeltaTimePrecise() const noexcept { return m_unscaledDeltaTime; }

        /**
         * @brief ゲーム開始からの経過時間を取得する（Time Scale適用後、float）
         * @details deltaTimeの累積値。一時停止中またはtimeScaleが0の間は進行しない。
         * @return float 経過時間（秒）
         */
        float timeSinceStartup() const noexcept { return static_cast<float>(m_gameTime); }

        /**
         * @brief ゲーム開始からの経過時間を取得する（Time Scale適用後、高精度）
         * @return double 経過時間（秒）
         */
        double timeSinceStartupPrecise() const noexcept { return m_gameTime; }

        /**
         * @brief ゲーム開始からの経過時間を取得する（タイムスケール未適用、float）
         * @details unscaledDeltaTimeの累積値。一時停止中は進行しないが、timeScaleが0でも進行する。
         * @return float 経過時間（秒）
         */
        float unscaledTime() const noexcept { return static_cast<float>(m_unscaledTime); }

        /**
         * @brief ゲーム開始からの経過時間を取得する（タイムスケール未適用、高精度）
         * @return double 経過時間（秒）
         */
        double unscaledTimePrecise() const noexcept { return m_unscaledTime; }

        /**
         * @brief 時間の進み方を取得する
         * @return float 倍率（1.0で等倍、0.5でスローモーション、0.0で停止）
         */
        float timeScale() const noexcept { return static_cast<float>(m_timeScale); }

        /**
         * @brief 時間の進み方を設定する
         * @param scale 倍率（1.0で等倍、0.0で停止、負数は禁止）
         */
        void setTimeScale(float scale) noexcept;

        /**
         * @brief Fixed Update用の1フレーム時間を取得する（float）
         * @details シミュレーション時間での固定刻み。timeScaleはこの値自体には適用せず、
         * scaled deltaTimeをAccumulatorに加算して実行回数を決める。
         * @return float Fixed Delta Time（秒）
         */
        float fixedDeltaTime() const noexcept { return static_cast<float>(m_fixedDeltaTime); }

        /**
         * @brief Fixed Update用の1フレーム時間を取得する（高精度）
         * @return double Fixed Delta Time（秒）
         */
        double fixedDeltaTimePrecise() const noexcept { return m_fixedDeltaTime; }

        /**
         * @brief Fixed Update用の1フレーム時間を設定する
         * @param deltaTime 設定する時間（秒、正数のみ）
         */
        void setFixedDeltaTime(float deltaTime) noexcept;

        /**
         * @brief Fixed Update が必要か判定する
         * @details 1フレームあたり最大8回。上限に達しても残る完全な刻みは破棄し、
         * 端数のみ次フレームへ持ち越す。
         * @return bool Fixed Update が必要ならtrue
         */
        bool hasFixedUpdate() const noexcept
        {
            return m_fixedAccumulator >= m_fixedDeltaTime
                && m_fixedUpdatesThisFrame < MAX_FIXED_UPDATES_PER_FRAME;
        }

        /**
         * @brief Fixed Update を1回実行済みにしてAccumulatorを減らす
         */
        void consumeFixedUpdate() noexcept;

        /**
         * @brief ゲーム開始からのフレーム数を取得する
         * @return uint64_t フレーム数
         */
        uint64_t frameCount() const noexcept { return m_frameCount; }

        /**
         * @brief Fixed Update が実行された総数を取得する
         * @return uint64_t Fixed Frame Count
         */
        uint64_t fixedFrameCount() const noexcept { return m_fixedFrameCount; }

        /**
         * @brief 直近フレームのFPSを取得する（瞬間値、float）
         * @return float FPS
         */
        float fps() const noexcept { return static_cast<float>(m_fps); }

        /**
         * @brief 直近の平均FPS を取得する（Time Scale未適用、float）
         * @return float 平均FPS
         */
        float averageFps() const noexcept { return static_cast<float>(m_averageFps); }

        /**
         * @brief 平滑化された Delta Time を取得する（float）
         * 直近10フレームのunscaledDeltaTimeの平均値を返す。一時停止中のフレームは0として含む。
         * @return float Smooth Delta Time（秒）
         */
        float smoothDeltaTime() const noexcept { return static_cast<float>(m_smoothDeltaTime); }

        /**
         * @brief ゲームが一時停止状態か判定する
         * @return bool 一時停止中ならtrue
         */
        bool isPaused() const noexcept { return m_paused; }

        /**
         * @brief ゲームの一時停止状態を設定する
         * @details 一時停止中は次回以降のunscaledDeltaTimeを0にし、ゲーム時間とFixed Updateの
         * 進行を停止する。実時間やフレーム処理そのものは停止しない。timeScaleとは独立している。
         * @param paused 一時停止するならtrue
         */
        void setPaused(bool paused) noexcept { m_paused = paused; }

        /**
         * @brief エンジン起動からの実時間を取得する（Time Scale非適用）
         * @details 一時停止中も進行し、deltaTimeの上限処理も適用されない。
         * @return double 経過時間（秒）
         */
        double realtimeSinceStartup() const noexcept;

        /**
         * @brief 1フレームの経過時間の上限を設定する
         * ブレークポイントなどで長時間停止した際のスパイク対策。
         * realtimeSinceStartupには適用されない。
         * @param seconds 上限（秒）
         */
        void setMaxDeltaTime(float seconds) noexcept;

        /**
         * @brief getDeltaTime（従来互換）
         * @return float 経過時間（秒、タイムスケール適用後）
         * @deprecated deltaTime() を使用してください
         */
        float getDeltaTime() const { return deltaTime(); }

        /**
         * @brief getUnscaledDeltaTime（従来互換）
         * @return float 経過時間（秒、タイムスケール未適用）
         * @deprecated unscaledDeltaTime() を使用してください
         */
        float getUnscaledDeltaTime() const { return unscaledDeltaTime(); }

        /**
         * @brief getTotalTime（従来互換）
         * @return float 経過時間（秒）
         * @deprecated timeSinceStartup() を使用してください
         */
        float getTotalTime() const { return timeSinceStartup(); }

        /**
         * @brief getFrameRate（従来互換）
         * @return float 平均フレームレート
         * @deprecated averageFps() を使用してください
         */
        float getFrameRate() const { return averageFps(); }

        /**
         * @brief getFrameCount（従来互換）
         * @return uint64_t フレーム数
         * @deprecated frameCount() を使用してください
         */
        uint64_t getFrameCount() const { return frameCount(); }

        /**
         * @brief getTimeScale（従来互換）
         * @return float タイムスケール
         * @deprecated timeScale() を使用してください
         */
        float getTimeScale() const { return timeScale(); }

    private:

        TimeManager();
        ~TimeManager() = default;

        GE_DISABLE_COPY_AND_MOVE(TimeManager);

        /**
         * @brief Smooth Delta Time を更新する
         */
        void updateSmoothDeltaTime() noexcept;

        /**
         * @brief FPS統計情報を更新する
         */
        void updateFpsStats() noexcept;

        static constexpr uint32_t SMOOTH_SAMPLE_COUNT = 10;         //!< Smooth Delta Time のサンプル数
        static constexpr double FPS_UPDATE_INTERVAL = 0.5;          //!< FPS統計を更新する間隔（秒）
        static constexpr uint32_t MAX_FIXED_UPDATES_PER_FRAME = 8;  //!< 1フレーム内の Fixed Update 上限数。超過分は破棄

        HighResolutionTimer m_timer;                                           //!< 高精度タイマー
        double m_deltaTime = 0.0;                                              //!< 前フレームからの経過時間（Time Scale適用後）
        double m_unscaledDeltaTime = 0.0;                                      //!< 前フレームからの経過時間（Time Scale未適用）
        double m_gameTime = 0.0;                                               //!< ゲーム開始からの経過時間（Time Scale適用後）
        double m_unscaledTime = 0.0;                                           //!< ゲーム開始からの経過時間（Time Scale未適用）
        double m_realtime = 0.0;                                               //!< エンジン起動からの実経過時間
        double m_timeScale = 1.0;                                              //!< 時間の進み方（1.0で等倍、0.0で停止）
        double m_maxDeltaTime = 0.1;                                           //!< 1フレームの経過時間の上限
        double m_fixedDeltaTime = 1.0 / 60.0;                                  //!< Fixed Update の1フレーム時間
        double m_fixedAccumulator = 0.0;                                       //!< Fixed Update のAccumulator
        uint64_t m_frameCount = 0;                                             //!< フレーム数
        uint64_t m_fixedFrameCount = 0;                                        //!< Fixed Update の実行回数
        uint32_t m_fixedUpdatesThisFrame = 0;                                  //!< 今フレームの Fixed Update 実行回数
        double m_fps = 0.0;                                                    //!< 現在のFPS
        double m_averageFps = 0.0;                                             //!< 平均FPS
        double m_fpsTimer = 0.0;                                               //!< FPS計測用タイマー
        uint32_t m_fpsFrameCount = 0;                                          //!< FPS計測用フレームカウント
        double m_smoothDeltaTime = 0.0;                                        //!< 平滑化された Delta Time
        uint32_t m_smoothBufferIndex = 0;                                      //!< バッファのインデックス
        bool m_paused = false;                                                 //!< ゲーム一時停止フラグ
        std::array<double, SMOOTH_SAMPLE_COUNT> m_smoothDeltaTimeBuffer = {};  //!< Smooth Delta Time 用バッファ
    };
} // namespace Engine
