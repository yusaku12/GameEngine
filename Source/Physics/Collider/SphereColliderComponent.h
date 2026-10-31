#pragma once

#include "Physics\Collider\ColliderComponent.h"

namespace Engine
{
    /**
     * @brief Sphere Physics Shapeを提供するCollider Component。
     */
    class SphereColliderComponent final : public ColliderComponent
    {
    public:

        /** @brief 球の半径を設定する。正の有限値のみ受け付ける。 */
        void setRadius(float radius) noexcept;
        /** @brief 球の半径を取得する。 */
        float getRadius() const noexcept { return m_radius; }

    protected:

        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;

    private:

        float m_radius = 0.5f;
    };
} // namespace Engine
