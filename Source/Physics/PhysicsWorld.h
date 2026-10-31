#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "Core\Math\MathTypes.h"
#include "Physics\PhysicsDebug.h"
#include "Physics\PhysicsQuery.h"

namespace Engine
{
    class ColliderComponent;
    class CharacterControllerComponent;
    class JointComponent;
    class PhysicsSystem;
    class RigidbodyComponent;

    /**
     * @brief PhysXシーンをエンジン側から隔離して管理する。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。シミュレーション実行中にコンポーネントを変更しないこと。
     */
    class PhysicsWorld final
    {
    public:

        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;

        /** @brief PhysicsWorldが初期化済みかどうかを取得する。 */
        bool isInitialized() const noexcept;

        /**
         * @brief PhysXシーンを固定時間刻みで1回進める。
         * @param fixedDeltaTime 固定時間刻み（秒）。
         */
        void simulate(float fixedDeltaTime) noexcept;

        /** @brief シミュレーション結果を取得し、動的Rigidbodyの状態をTransformへ反映する。 */
        void fetchResults() noexcept;

        /** @brief シミュレーションコールバックがキューに保存したイベントをゲームプレイ側へ通知する。 */
        void dispatchEvents() noexcept;

        /** @brief 現在の物理状態のデバッグ表示を登録する。 */
        void drawDebug(const PhysicsDebugSettings& settings) const noexcept;
        /** @brief 有効なボディ、シェイプ、ジョイントの数を取得する。 */
        PhysicsWorldStatistics getStatistics() const noexcept;
        /** @brief 動的ボディとコントローラーを初期姿勢に戻し、接触状態をクリアする。 */
        bool reset() noexcept;

        /**
         * @brief レイキャストを行い、最も近いコライダーを検索する。
         * @param origin レイのワールド座標系での始点。
         * @param direction レイの方向。正規化されていなくてもよい。
         * @param maxDistance 最大距離。
         * @param filter シーン照会フィルター。
         * @return ヒット情報。対象がなければstd::nullopt。
         */
        std::optional<RaycastHit> raycast(
            const Vector3& origin,
            const Vector3& direction,
            float maxDistance,
            const PhysicsQueryFilter& filter = {}) const noexcept;

        /**
         * @brief レイ上のコライダーを距離順で検索する。
         * @param origin レイのワールド座標系での始点。
         * @param direction レイの方向。正規化されていなくてもよい。
         * @param maxDistance 最大距離。
         * @param maxHits 最大返却数。PhysXヒットバッファーの上限でもある。
         * @param filter シーン照会フィルター。
         */
        std::vector<RaycastHit> raycastAll(
            const Vector3& origin,
            const Vector3& direction,
            float maxDistance,
            std::uint32_t maxHits = 128,
            const PhysicsQueryFilter& filter = {}) const noexcept;

        /**
         * @brief 球を指定方向へスイープする。
         * @param origin 球のワールド座標系での中心。
         * @param radius 球の半径。
         * @param direction スイープ方向。正規化されていなくてもよい。
         * @param maxDistance 最大距離。
         * @param filter シーン照会フィルター。
         * @return 最も近いヒット情報。対象がなければstd::nullopt。
         */
        std::optional<SweepHit> sphereCast(
            const Vector3& origin,
            float radius,
            const Vector3& direction,
            float maxDistance,
            const PhysicsQueryFilter& filter = {}) const noexcept;

        /**
         * @brief ワールドY軸に沿ったカプセルを指定方向へスイープする。
         * @param center カプセルのワールド座標系での中心。
         * @param radius カプセルの半径。
         * @param halfHeight カプセル中心から半球の中心までの距離。
         * @param direction スイープ方向。正規化されていなくてもよい。
         * @param maxDistance 最大距離。
         * @param filter シーン照会フィルター。
         * @return 最も近いヒット情報。対象がなければstd::nullopt。
         */
        std::optional<SweepHit> capsuleCast(
            const Vector3& center,
            float radius,
            float halfHeight,
            const Vector3& direction,
            float maxDistance,
            const PhysicsQueryFilter& filter = {}) const noexcept;

        /**
         * @brief 球と重なるGameObjectのGUIDを検索する。
         * @param center 球のワールド座標系での中心位置。
         * @param radius 球の半径。
         * @param maxHits 最大返却数。PhysXヒットバッファーの上限でもある。
         * @param filter シーン照会フィルター。
         */
        PhysicsOverlapResults overlapSphere(
            const Vector3& center,
            float radius,
            std::uint32_t maxHits = 128,
            const PhysicsQueryFilter& filter = {}) const noexcept;

        /** @brief 重力を取得する。 */
        Vector3 getGravity() const noexcept;

        /**
         * @brief 重力を設定する。
         * @param gravity ワールド座標系での重力。
         */
        void setGravity(const Vector3& gravity) noexcept;

    private:

        friend class ColliderComponent;
        friend class CharacterControllerComponent;
        friend class JointComponent;
        friend class PhysicsSystem;
        friend class RigidbodyComponent;

        struct Impl;

        bool initialize() noexcept;
        void shutdown() noexcept;
        void setDebugCapture(bool drawContacts, bool drawRaycasts) noexcept;

        bool createRigidbody(RigidbodyComponent& component) noexcept;
        void destroyRigidbody(RigidbodyComponent& component) noexcept;
        void forgetRigidbodyReferences(RigidbodyComponent& component) noexcept;
        void updateRigidbody(RigidbodyComponent& component) noexcept;
        void setSleeping(RigidbodyComponent& component, bool sleeping) noexcept;
        bool addForce(RigidbodyComponent& component, const Vector3& force, bool impulse) noexcept;
        bool addTorque(RigidbodyComponent& component, const Vector3& torque) noexcept;

        bool createCollider(ColliderComponent& component) noexcept;
        void destroyCollider(ColliderComponent& component) noexcept;
        void updateCollider(ColliderComponent& component) noexcept;

        bool createController(CharacterControllerComponent& component) noexcept;
        void destroyController(CharacterControllerComponent& component) noexcept;
        void updateController(CharacterControllerComponent& component) noexcept;
        bool moveController(
            CharacterControllerComponent& component,
            const Vector3& displacement,
            float deltaTime) noexcept;

        bool createJoint(JointComponent& component) noexcept;
        void destroyJoint(JointComponent& component) noexcept;
        void updateJoint(JointComponent& component) noexcept;

        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine
