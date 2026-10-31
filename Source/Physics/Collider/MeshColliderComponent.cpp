#include "Pch.h"
#include "Physics\Collider\MeshColliderComponent.h"

namespace Engine
{
    bool MeshColliderComponent::setPhysicsAsset(
        std::shared_ptr<const PhysicsMeshAsset> asset) noexcept
    {
        if (asset == nullptr || asset->getType() != PhysicsMeshType::Triangle)
        {
            LOG_ERROR("[MeshColliderComponent] A cooked triangle-mesh Physics asset is required.");
            return false;
        }
        m_asset = std::move(asset);
        refreshGeometry();
        notifyPayloadRestored();
        return true;
    }

    PhysicsShapeGeometry MeshColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        return {
            PhysicsShapeType::TriangleMesh,
            Vector3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)),
            m_asset };
    }
} // namespace Engine
