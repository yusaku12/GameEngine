#pragma once

#include "Physics\Collider\ColliderComponent.h"

namespace Engine
{
    /**
     * @brief Box Physics Shapeを提供するCollider Component。
     */
    class BoxColliderComponent final : public ColliderComponent
    {
    public:

        /** @brief ボックス形状の全体サイズを設定する。各軸は正の有限値である必要がある。 */
        void setSize(const Vector3& size) noexcept;
        /** @brief ボックス形状の全体サイズを取得する。 */
        const Vector3& getSize() const noexcept { return m_size; }

    protected:

        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;

    private:

        Vector3 m_size = Vector3::One;
    };
} // namespace Engine
