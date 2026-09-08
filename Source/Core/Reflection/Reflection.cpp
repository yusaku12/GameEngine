#include "Pch.h"
#include "Core\Reflection\Reflection.h"

namespace Engine
{
    ReflectionRegistry& ReflectionRegistry::instance() noexcept
    {
        static ReflectionRegistry registry;
        return registry;
    }

    const TypeInfo* ReflectionRegistry::get(const std::type_index type) const noexcept
    {
        const auto found = m_types.find(type);
        return found == m_types.end() ? nullptr : &found->second;
    }

    void ReflectionRegistry::clear() noexcept
    {
        m_types.clear();
    }
} // namespace Engine