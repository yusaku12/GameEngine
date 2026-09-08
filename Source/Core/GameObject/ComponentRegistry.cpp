#include "Pch.h"
#include "Core\GameObject\ComponentRegistry.h"

namespace Engine
{
    ComponentRegistry& ComponentRegistry::instance() noexcept
    {
        static ComponentRegistry registry;
        return registry;
    }

    const ComponentTypeInfo* ComponentRegistry::get(const ComponentTypeID id) const noexcept
    {
        const auto found = m_ids.find(id);
        if (found == m_ids.end())
            return nullptr;
        const auto type = m_types.find(found->second);
        return type == m_types.end() ? nullptr : &type->second;
    }

    const ComponentTypeInfo* ComponentRegistry::get(const std::type_index type) const noexcept
    {
        const auto found = m_types.find(type);
        return found == m_types.end() ? nullptr : &found->second;
    }

    const ComponentTypeInfo* ComponentRegistry::findByName(const std::string_view name) const noexcept
    {
        for (const auto& [type, info] : m_types)
        {
            GE_UNUSED(type);
            if (info.name == name)
                return &info;
        }
        return nullptr;
    }

    void ComponentRegistry::clear() noexcept
    {
        m_types.clear();
        m_ids.clear();
        m_nextId = 1;
    }
} // namespace Engine