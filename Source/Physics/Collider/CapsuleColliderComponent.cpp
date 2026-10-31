#include "Pch.h"
#include "Physics\Collider\CapsuleColliderComponent.h"

namespace Engine
{
    void CapsuleColliderComponent::setRadius(const float radius) noexcept
    {
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            LOG_ERROR("[CapsuleColliderComponent] Radius must be a positive finite value.");
            return;
        }
        m_radius = radius;
        m_height = (std::max)(m_height, 2.0f * radius);
        refreshGeometry();
    }

    void CapsuleColliderComponent::setHeight(const float height) noexcept
    {
        if (!std::isfinite(height) || height <= 0.0f)
        {
            LOG_ERROR("[CapsuleColliderComponent] Height must be a positive finite value.");
            return;
        }
        m_height = (std::max)(height, 2.0f * m_radius);
        refreshGeometry();
    }

    PhysicsShapeGeometry CapsuleColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        const float radialScale = (std::max)(std::abs(scale.x), std::abs(scale.z));
        const float radius = m_radius * radialScale;
        const float halfHeight = (std::max)(0.0f, m_height * std::abs(scale.y) * 0.5f - radius);
        return { PhysicsShapeType::Capsule, Vector3(radius, halfHeight, 0.0f) };
    }
} // namespace Engine
