#pragma once

#include "Physics\Collider\ColliderComponent.h"

namespace Engine
{
    /**
     * @brief オフライン調理済みのPhysicsMeshAssetを使用する静的三角形メッシュコライダー。
     */
    class MeshColliderComponent final : public ColliderComponent
    {
    public:

        /** @brief 調理済みの三角形メッシュデータを設定する。三角形メッシュはトリガーまたは動的シェイプとして使用できない。 */
        bool setPhysicsAsset(std::shared_ptr<const PhysicsMeshAsset> asset) noexcept;
        /** @brief 設定されている調理済み物理アセットを取得する。 */
        const std::shared_ptr<const PhysicsMeshAsset>& getPhysicsAsset() const noexcept { return m_asset; }

    protected:

        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;
        bool supportsTrigger() const noexcept override { return false; }

    private:

        std::shared_ptr<const PhysicsMeshAsset> m_asset;
    };
} // namespace Engine
