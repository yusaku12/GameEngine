#include "Pch.h"
#include "Physics\Collider\BoxColliderComponent.h"

namespace Engine
{
    void BoxColliderComponent::setSize(const Vector3& size) noexcept
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z)
            || size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f)
        {
            LOG_ERROR("[BoxColliderComponent] Size must be positive and finite on every axis.");
            return;
        }
        m_size = size;
        refreshGeometry();
    }

    PhysicsShapeGeometry BoxColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        return {
            PhysicsShapeType::Box,
            Vector3(
                m_size.x * std::abs(scale.x) * 0.5f,
                m_size.y * std::abs(scale.y) * 0.5f,
                m_size.z * std::abs(scale.z) * 0.5f)
        };
    }
} // namespace Engine
