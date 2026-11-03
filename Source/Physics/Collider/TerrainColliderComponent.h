#pragma once

#include "Physics\Collider\ColliderComponent.h"
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
        /** @brief Gets immutable cooked geometry, or null before activation/generation. */
        const std::shared_ptr<const PhysicsMeshAsset>& getPhysicsAsset() const noexcept { return m_asset; }

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
        bool replaceAsset(std::shared_ptr<const PhysicsMeshAsset> asset, ModelHandle source) noexcept;
        bool generateAsset();
        void detachTerrain() noexcept;
        std::shared_ptr<const PhysicsMeshAsset> m_asset;
        ModelHandle m_sourceModel;
        bool m_active = false;
    };
}
