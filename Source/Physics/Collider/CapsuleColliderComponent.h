#pragma once

#include "Physics\Collider\ColliderComponent.h"

namespace Engine
{
    /**
     * @brief Y-up Capsule Physics Shapeを提供するCollider Component。
     */
    class CapsuleColliderComponent final : public ColliderComponent
    {
    public:

        /** @brief カプセルの半径を設定する。正の有限値のみ受け付ける。 */
        void setRadius(float radius) noexcept;
        /** @brief カプセルの半径を取得する。 */
        float getRadius() const noexcept { return m_radius; }

        /** @brief カプセルの全高を設定する。直径より小さい値は直径に補正する。 */
        void setHeight(float height) noexcept;
        /** @brief カプセルの全高を取得する。 */
        float getHeight() const noexcept { return m_height; }

    protected:

        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;

    private:

        float m_radius = 0.5f;
        float m_height = 2.0f;
    };
} // namespace Engine
