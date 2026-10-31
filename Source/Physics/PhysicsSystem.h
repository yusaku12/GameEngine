#pragma once

#include <memory>
#include <mutex>
#include <array>

#include "Physics\PhysicsDebug.h"

namespace Engine
{
    class PhysicsWorld;

    /**
     * @brief エンジン全体の物理ワールドとPhysXのライフサイクルを管理する。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。シミュレーションはPhysXディスパッチャー上で実行される。
     */
    class PhysicsSystem final
    {
    public:

        /** @brief PhysicsSystemのシングルトンを取得する。 */
        static PhysicsSystem& instance() noexcept;

        PhysicsSystem() = default;
        ~PhysicsSystem();

        PhysicsSystem(const PhysicsSystem&) = delete;
        PhysicsSystem& operator=(const PhysicsSystem&) = delete;
        PhysicsSystem(PhysicsSystem&&) = delete;
        PhysicsSystem& operator=(PhysicsSystem&&) = delete;

        /**
         * @brief PhysXとPhysicsWorldを初期化する。
         * @return 初期化に成功した場合はtrue。
         */
        bool initialize() noexcept;

        /** @brief コンポーネントを破棄した後にPhysXのリソースを安全に解放する。 */
        void shutdown() noexcept;

        /**
         * @brief 固定時間刻みのシミュレーションを1回実行する。
         * @param fixedDeltaTime 固定時間刻み（秒）。
         */
        void update(float fixedDeltaTime) noexcept;

        /** @brief 物理イベントキューに蓄積されたイベントをゲームプレイ側へ通知する。 */
        void dispatchEvents() noexcept;
        /** @brief 現在の物理デバッグ用ジオメトリを描画フレームごとに1回登録する。 */
        void drawDebug() noexcept;
        /** @brief GameObjectの更新を止めずに物理演算を一時停止または再開する。 */
        void setPaused(bool paused) noexcept;
        /** @brief 物理演算が一時停止中かどうかを取得する。 */
        bool isPaused() const noexcept;
        /** @brief ゲームプレイ側から現在の物理演算ステップへの変更登録が可能かどうかを返す。 */
        bool canAdvancePhysics() const noexcept;
        /** @brief 一時停止中に固定時間刻みの物理演算を1回だけ実行するよう要求する。 */
        bool requestSingleStep() noexcept;
        /** @brief 保留中の単一ステップ実行要求を処理する。 */
        void updateFrame(float fixedDeltaTime) noexcept;
        /** @brief 物理ボディとコントローラーを初期姿勢に戻す。 */
        bool reset() noexcept;

        /** @brief 管理中のPhysicsWorldを取得する。未初期化の場合はnullptrを返す。 */
        PhysicsWorld* getWorld() noexcept { return m_world.get(); }
        const PhysicsWorld* getWorld() const noexcept { return m_world.get(); }

        /** @brief 物理システムが初期化済みかどうかを取得する。 */
        bool isInitialized() const noexcept;

        /** @brief エディターで設定されたデバッグ描画オプションを取得する。 */
        PhysicsDebugSettings getDebugSettings() const noexcept;
        /** @brief エディターで設定されたデバッグ描画オプションを更新する。 */
        void setDebugSettings(const PhysicsDebugSettings& settings) noexcept;
        /** @brief 現在の物理プロファイラー情報を取得する。 */
        PhysicsProfilerSnapshot getProfilerSnapshot() const noexcept;

    private:

        std::unique_ptr<PhysicsWorld> m_world;
        PhysicsDebugSettings m_debugSettings;
        PhysicsProfilerSnapshot m_profilerSnapshot;
        std::array<double, 120> m_stepDurations{};
        std::size_t m_stepDurationCursor = 0;
        std::size_t m_stepDurationCount = 0;
        bool m_paused = false;
        bool m_stepRequested = false;
        mutable std::mutex m_debugMutex;
    };
} // namespace Engine
