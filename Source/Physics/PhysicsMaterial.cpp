#include "Pch.h"
#include "Physics\PhysicsMaterial.h"

namespace Engine
{
    bool PhysicsMaterial::setStaticFriction(const float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.0f)
        {
            LOG_ERROR("[PhysicsMaterial] Static friction must be finite and non-negative.");
            return false;
        }
        m_staticFriction = value;
        return true;
    }

    bool PhysicsMaterial::setDynamicFriction(const float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.0f)
        {
            LOG_ERROR("[PhysicsMaterial] Dynamic friction must be finite and non-negative.");
            return false;
        }
        m_dynamicFriction = value;
        return true;
    }

    bool PhysicsMaterial::setRestitution(const float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
        {
            LOG_ERROR("[PhysicsMaterial] Restitution must be finite and within [0, 1].");
            return false;
        }
        m_restitution = value;
        return true;
    }
} // namespace Engine
