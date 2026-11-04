#include "Pch.h"
#include "Components\Physics\Collider\SphereColliderComponent.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"

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
