#include "Pch.h"
#include "Physics\Collider\ConvexColliderComponent.h"

namespace Engine
{
    bool ConvexColliderComponent::setPhysicsAsset(
        std::shared_ptr<const PhysicsMeshAsset> asset) noexcept
    {
        if (asset == nullptr || asset->getType() != PhysicsMeshType::Convex)
        {
            LOG_ERROR("[ConvexColliderComponent] A cooked convex-mesh Physics asset is required.");
            return false;
        }
        m_asset = std::move(asset);
        refreshGeometry();
        notifyPayloadRestored();
        return true;
    }

    PhysicsShapeGeometry ConvexColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        return {
            PhysicsShapeType::ConvexMesh,
            Vector3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)),
            m_asset };
    }
} // namespace Engine
