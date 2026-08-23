#include "Pch.h"

#include "Core\Reflection\Type.h"

namespace Engine
{

Any Method::invoke(void* instance, const Any* args, size_t count) const
{
    if (m_invoker == nullptr)
        return Any();

    GE_ASSERT_MSG(count == m_argCount, "メソッド {} の引数の数が一致しません（要求: {} / 指定: {}）", m_name, m_argCount, count);

    if (count != m_argCount)
        return Any();

    return m_invoker(instance, args, count);
}

const Property* Type::findProperty(std::string_view name) const
{
    for (const Property& property : m_properties)
    {
        if (property.getName() == name)
            return &property;
    }

    return m_baseType != nullptr ? m_baseType->findProperty(name) : nullptr;
}

const Method* Type::findMethod(std::string_view name) const
{
    for (const Method& method : m_methods)
    {
        if (method.getName() == name)
            return &method;
    }

    return m_baseType != nullptr ? m_baseType->findMethod(name) : nullptr;
}

bool Type::isDerivedFrom(const Type& other) const
{
    for (const Type* current = m_baseType; current != nullptr; current = current->m_baseType)
    {
        if (current->m_id == other.m_id)
            return true;
    }

    return false;
}

std::string Type::toString(const void* instance) const
{
    if (instance == nullptr)
        return m_name + " { null }";

    std::string result = m_name;
    result += " { ";

    bool first = true;
    for (const Type* current = this; current != nullptr; current = current->m_baseType)
    {
        for (const Property& property : current->m_properties)
        {
            if (!first)
                result += ", ";

            result += property.getName();
            result += " = ";
            result += property.getValue(instance).toString();
            first = false;
        }
    }

    result += " }";
    return result;
}

std::string_view EnumType::toString(int64_t value) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.value == value)
            return entry.name;
    }

    return std::string_view();
}

bool EnumType::fromString(std::string_view name, int64_t& outValue) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.name == name)
        {
            outValue = entry.value;
            return true;
        }
    }

    return false;
}

} // namespace Engine
