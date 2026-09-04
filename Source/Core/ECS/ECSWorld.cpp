#include "Pch.h"

#include "Core\ECS\ECSWorld.h"

namespace Engine
{
    Entity ECSWorld::CreateEntity()
    {
        return m_registry.create();
    }

    void ECSWorld::DestroyEntity(Entity entity)
    {
        if (IsValid(entity))
            m_registry.destroy(entity);
    }

    bool ECSWorld::IsValid(Entity entity) const noexcept
    {
        return m_registry.valid(entity);
    }

    void ECSWorld::Clear()
    {
        m_registry.clear();
    }
} // namespace Engine
