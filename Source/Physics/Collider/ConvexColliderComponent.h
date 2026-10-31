#pragma once

#include "Physics\Collider\ColliderComponent.h"

namespace Engine
{
    /**
     * @brief オフライン調理済みの凸PhysicsMeshAssetを使用する凸コライダー。
     */
    class ConvexColliderComponent final : public ColliderComponent
    {
    public:

        /** @brief 調理済みの凸メッシュデータを設定する。 */
        bool setPhysicsAsset(std::shared_ptr<const PhysicsMeshAsset> asset) noexcept;
        /** @brief 設定されている調理済み物理アセットを取得する。 */
        const std::shared_ptr<const PhysicsMeshAsset>& getPhysicsAsset() const noexcept { return m_asset; }

    protected:

        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;

    private:

        std::shared_ptr<const PhysicsMeshAsset> m_asset;
    };
} // namespace Engine
