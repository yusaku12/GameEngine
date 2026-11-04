#pragma once

#include "Core\GameObject\Component.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class PhysicsWorld;

    /**
     * @brief PhysXの動的／キネマティックアクターをGameObjectに関連付けるコンポーネント。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。
     */
    class RigidbodyComponent final : public Component
    {
    public:

        RigidbodyComponent() = default;
        ~RigidbodyComponent() override = default;

        /** @brief Rigidbodyの質量を設定する。正の有限値のみ受け付ける。 */
        void setMass(float mass) noexcept;
        /** @brief Rigidbodyの質量を取得する。 */
        float getMass() const noexcept { return m_mass; }

        /** @brief 線速度を設定する。 */
        void setLinearVelocity(const Vector3& velocity) noexcept;
        /** @brief 線速度を取得する。 */
        Vector3 getLinearVelocity() const noexcept { return m_linearVelocity; }

        /** @brief 角速度を設定する。 */
        void setAngularVelocity(const Vector3& velocity) noexcept;
        /** @brief 角速度を取得する。 */
        Vector3 getAngularVelocity() const noexcept { return m_angularVelocity; }

        /** @brief 力を加える。 */
        void addForce(const Vector3& force) noexcept;
        /** @brief 力積を加える。 */
        void addImpulse(const Vector3& impulse) noexcept;
        /** @brief トルクを加える。 */
        void addTorque(const Vector3& torque) noexcept;

        /** @brief キネマティックモードを切り替える。 */
        void setKinematic(bool enabled) noexcept;
        /** @brief キネマティックモードかどうかを取得する。 */
        bool isKinematic() const noexcept { return m_isKinematic; }

        /** @brief 重力の適用を切り替える。 */
        void setGravityEnabled(bool enabled) noexcept;
        /** @brief 重力が有効か取得する。 */
        bool isGravityEnabled() const noexcept { return m_gravityEnabled; }

        /** @brief 線形減衰を設定する。 */
        void setLinearDamping(float damping) noexcept;
        /** @brief 線形減衰を取得する。 */
        float getLinearDamping() const noexcept { return m_linearDamping; }
        /** @brief 角減衰を設定する。 */
        void setAngularDamping(float damping) noexcept;
        /** @brief 角減衰を取得する。 */
        float getAngularDamping() const noexcept { return m_angularDamping; }

        /** @brief 動的Rigidbodyを起床させる。キネマティックモードではエラーを記録し、操作を拒否する。 */
        void wakeUp() noexcept;
        /** @brief 動的Rigidbodyを休眠させる。キネマティックモードではエラーを記録し、操作を拒否する。 */
        void sleep() noexcept;

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:

        void onImGui() override;
        void onPayloadRestored() override;
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;

    private:

        friend class PhysicsWorld;

        void synchronizeTransform(const Vector3& position, const Quaternion& rotation) noexcept;

        float m_mass = 1.0f;
        float m_linearDamping = 0.0f;
        float m_angularDamping = 0.05f;
        Vector3 m_linearVelocity = Vector3::Zero;
        Vector3 m_angularVelocity = Vector3::Zero;
        bool m_isKinematic = false;
        bool m_gravityEnabled = true;
    };
} // namespace Engine
