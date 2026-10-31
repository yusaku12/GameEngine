#pragma once

namespace Engine
{
    /**
     * @brief Colliderで使用する摩擦係数と反発係数を保持するエンジン側の値型。
     * @details 各Colliderはこの値をPhysXマテリアルに適用する。
     */
    class PhysicsMaterial final
    {
    public:

        PhysicsMaterial() = default;

        /** @brief 静止摩擦係数を取得する。 */
        float getStaticFriction() const noexcept { return m_staticFriction; }
        /** @brief 動摩擦係数を取得する。 */
        float getDynamicFriction() const noexcept { return m_dynamicFriction; }
        /** @brief 0から1の範囲の反発係数を取得する。 */
        float getRestitution() const noexcept { return m_restitution; }

        /** @brief 有限かつ0以上の静止摩擦係数を設定する。 */
        bool setStaticFriction(float value) noexcept;
        /** @brief 有限かつ0以上の動摩擦係数を設定する。 */
        bool setDynamicFriction(float value) noexcept;
        /** @brief 有限かつ0から1の範囲の反発係数を設定する。 */
        bool setRestitution(float value) noexcept;

    private:

        float m_staticFriction = 0.5f;
        float m_dynamicFriction = 0.5f;
        float m_restitution = 0.0f;
    };
} // namespace Engine
