#include "Pch.h"

#include "Core\Reflection\TypeRegistry.h"

namespace Engine
{

const Type* TypeRegistry::findType(TypeId id) const
{
    const auto found = m_types.find(id);
    return found != m_types.end() ? found->second.get() : nullptr;
}

const Type* TypeRegistry::findType(std::string_view name) const
{
    const auto found = m_typesByName.find(std::string(name));
    return found != m_typesByName.end() ? found->second : nullptr;
}

const EnumType* TypeRegistry::findEnum(TypeId id) const
{
    const auto found = m_enums.find(id);
    return found != m_enums.end() ? found->second.get() : nullptr;
}

const EnumType* TypeRegistry::findEnum(std::string_view name) const
{
    const auto found = m_enumsByName.find(std::string(name));
    return found != m_enumsByName.end() ? found->second : nullptr;
}

Type* TypeRegistry::findMutableType(TypeId id)
{
    const auto found = m_types.find(id);
    return found != m_types.end() ? found->second.get() : nullptr;
}

void TypeRegistry::dump() const
{
    LOG_INFO("[Reflection] 登録済みの型: {} 件 / 列挙型: {} 件", m_types.size(), m_enums.size());

    for (const auto& entry : m_types)
    {
        const Type& type = *entry.second;
        LOG_INFO("[Reflection]   {} (size={} / プロパティ {} 件 / メソッド {} 件)",
            type.getName(),
            type.getSize(),
            type.getProperties().size(),
            type.getMethods().size());
    }
}

} // namespace Engine
