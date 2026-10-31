#include "Pch.h"
#include "Physics\Collider\SphereColliderComponent.h"

namespace Engine
{
    void SphereColliderComponent::setRadius(const float radius) noexcept
    {
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            LOG_ERROR("[SphereColliderComponent] Radius must be a positive finite value.");
            return;
        }
        m_radius = radius;
        refreshGeometry();
    }

    PhysicsShapeGeometry SphereColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        const float maxScale = (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        return { PhysicsShapeType::Sphere, Vector3(m_radius * maxScale, 0.0f, 0.0f) };
    }
} // namespace Engine
