#pragma once

#include "Components\Physics\Collider\ColliderComponent.h"
#include "Assets\Model\ModelTypes.h"

namespace Engine
{
    class TerrainComponent;

    /**
     * @brief Optional static PhysX triangle collider generated from a Terrain on the same GameObject.
     * @details Uses the common collider material, layer and query/event paths. No external .physics asset is required.
     * Not a trigger or Rigidbody shape. Terrain visibility and collider enablement are independent.
     * @thread_safety Main/game thread only, between PhysX simulation steps.
     */
    class TerrainColliderComponent final : public ColliderComponent
    {
    public:
        /** @brief Gets the first chunk's immutable geometry, or null before generation; use getChunkAssets for all chunks. */
        const std::shared_ptr<const PhysicsMeshAsset>& getPhysicsAsset() const noexcept
        {
            static const std::shared_ptr<const PhysicsMeshAsset> empty;
            return m_assets.empty() ? empty : m_assets.front();
        }
        /** @brief Gets row-major cooked chunk assets; no ownership of PhysX backend objects is exposed. */
        std::span<const std::shared_ptr<const PhysicsMeshAsset>> getChunkAssets() const noexcept { return m_assets; }

    protected:
        PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept override;
        bool supportsTrigger() const noexcept override { return false; }
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;
        void onPayloadRestored() override;
        void onFixedUpdate(float deltaTime) override;
        void onImGui() override;

    private:
        friend class TerrainComponent;
        bool replaceAssets(std::vector<std::shared_ptr<const PhysicsMeshAsset>> assets, ModelHandle source) noexcept;
        bool generateAsset();
        void detachTerrain() noexcept;
        std::vector<std::shared_ptr<const PhysicsMeshAsset>> m_assets;
        ModelHandle m_sourceModel;
        bool m_active = false;
    };
}
