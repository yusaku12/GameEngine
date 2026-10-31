#pragma once

#include <array>
#include <cstdint>

#include "Core\GameObject\Component.h"

namespace Engine
{
    class PhysicsWorld;
    class RigidbodyComponent;

    enum class PhysicsJointType : std::uint8_t
    {
        Fixed,
        Revolute,
        Distance,
        D6
    };

    enum class PhysicsJointAxis : std::uint8_t
    {
        X,
        Y,
        Z,
        Twist,
        Swing1,
        Swing2
    };

    /**
     * @brief 所有元GameObjectのRigidbodyを別のボディまたはワールドに接続する。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。
     */
    class JointComponent final : public Component
    {
    public:

        /** @brief ジョイント拘束の種類を設定する。 */
        void setType(PhysicsJointType type) noexcept;
        /** @brief ジョイント拘束の種類を取得する。 */
        PhysicsJointType getType() const noexcept { return m_type; }

        /**
         * @brief 接続先ボディを設定する。接続先は省略可能。
         * @details 非所有参照。いずれかのRigidbodyが無効な間、ジョイントは解放される。
         *          ボディがnullの場合、ジョイントをワールドに固定する。
         */
        void setConnectedBody(RigidbodyComponent* body) noexcept;
        /** @brief 非所有参照である接続先ボディを取得する。未設定の場合はnullptrを返す。 */
        RigidbodyComponent* getConnectedBody() const noexcept { return m_connectedBody; }

        /** @brief このボディ側のローカルジョイントアンカーを設定する。 */
        void setLocalAnchor(const Vector3& anchor) noexcept;
        /** @brief このボディ側のローカルジョイントアンカーを取得する。 */
        const Vector3& getLocalAnchor() const noexcept { return m_localAnchor; }

        /** @brief 接続先ボディ側のローカルジョイントアンカーを設定する。 */
        void setConnectedAnchor(const Vector3& anchor) noexcept;
        /** @brief 接続先ボディ側のローカルジョイントアンカーを取得する。 */
        const Vector3& getConnectedAnchor() const noexcept { return m_connectedAnchor; }

        /** @brief 破断する力とトルクのしきい値を設定する。0以上の値を指定する。 */
        void setBreakForce(float force, float torque) noexcept;
        /** @brief ジョイントが破断する力のしきい値を取得する。 */
        float getBreakForce() const noexcept { return m_breakForce; }
        /** @brief ジョイントが破断するトルクのしきい値を取得する。 */
        float getBreakTorque() const noexcept { return m_breakTorque; }
        /** @brief 接続されたアクター同士の衝突を許可するか設定する。 */
        void setCollisionEnabled(bool enabled) noexcept;
        /** @brief 接続されたアクター同士の衝突が許可されているか取得する。 */
        bool isCollisionEnabled() const noexcept { return m_collisionEnabled; }

        /**
         * @brief Distanceジョイントの最小距離と最大距離を設定する。
         * @return 範囲が有効な場合はtrue。
         */
        bool setDistanceRange(float minimum, float maximum) noexcept;
        /** @brief 設定されたDistanceジョイントの範囲を取得する。 */
        void getDistanceRange(float& minimum, float& maximum) const noexcept
        {
            minimum = m_distanceMinimum;
            maximum = m_distanceMaximum;
        }
        /** @brief 距離範囲が設定されているか取得する。 */
        bool hasDistanceRange() const noexcept { return m_distanceRangeSet; }
        /** @brief 設定済みの距離範囲を解除する。 */
        void clearDistanceRange() noexcept;

        /** @brief D6の自由度を1つ有効または無効にする。 */
        void setD6AxisFree(PhysicsJointAxis axis, bool free) noexcept;
        /** @brief D6の自由度が解放されているか取得する。 */
        bool isD6AxisFree(PhysicsJointAxis axis) const noexcept;

        /**
         * @brief Revoluteジョイントの角度制限を有効にする。
         * @param minimum 最小角度（ラジアン）。
         * @param maximum 最大角度（ラジアン）。
         * @return 制限値が有効な場合はtrue。
         */
        bool setRevoluteLimit(float minimum, float maximum) noexcept;
        /** @brief 設定されたRevoluteジョイントの角度制限を取得する。 */
        void getRevoluteLimit(float& minimum, float& maximum) const noexcept
        {
            minimum = m_revoluteMinimum;
            maximum = m_revoluteMaximum;
        }
        /** @brief Revoluteジョイントの角度制限が設定されているか取得する。 */
        bool hasRevoluteLimit() const noexcept { return m_revoluteLimitSet; }
        /** @brief 設定済みのRevoluteジョイントの角度制限を解除する。 */
        void clearRevoluteLimit() noexcept;

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        bool resolveObjectReferences(const ObjectReferenceMap& objects) override;

    protected:

        void onImGui() override;
        void onPayloadRestored() override;
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;

    private:

        friend class PhysicsWorld;

        void refreshJoint() noexcept;

        RigidbodyComponent* m_connectedBody = nullptr;
        Vector3 m_localAnchor = Vector3::Zero;
        Vector3 m_connectedAnchor = Vector3::Zero;
        std::array<bool, 6> m_d6FreeAxes{};
        PhysicsJointType m_type = PhysicsJointType::Fixed;
        float m_breakForce = 3.402823466e+38F;
        float m_breakTorque = 3.402823466e+38F;
        float m_distanceMinimum = 0.0f;
        float m_distanceMaximum = 0.0f;
        float m_revoluteMinimum = 0.0f;
        float m_revoluteMaximum = 0.0f;
        bool m_distanceRangeSet = false;
        bool m_revoluteLimitSet = false;
        bool m_collisionEnabled = false;
        ObjectGUID m_pendingConnectedBodyGuid{};
    };
} // namespace Engine
