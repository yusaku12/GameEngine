#pragma once

#include <cstddef>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Engine
{
    enum class PropertyType
    {
        Boolean,
        Integer,
        UnsignedInteger,
        Float,
        Vector3,
        Quaternion,
        String
    };

    struct PropertyInfo
    {
        std::string_view name;
        PropertyType type = PropertyType::Float;
        std::size_t offset = 0;
        std::size_t size = 0;
        bool editable = false;
    };

    struct TypeInfo
    {
        std::string_view name;
        std::vector<PropertyInfo> properties;
    };

    /**
     * @brief Editor向け型・PropertyメタデータRegistry。
     * @thread_safety Main thread only.
     */
    class ReflectionRegistry
    {
    public:
        static ReflectionRegistry& instance() noexcept;

        template <typename T>
        TypeInfo& registerType(std::string_view name)
        {
            return m_types[std::type_index(typeid(T))] = TypeInfo{ name, {} };
        }

        template <typename T>
        TypeInfo* get() noexcept
        {
            const auto found = m_types.find(std::type_index(typeid(T)));
            return found == m_types.end() ? nullptr : &found->second;
        }

        [[nodiscard]] const TypeInfo* get(std::type_index type) const noexcept;
        void clear() noexcept;

    private:
        std::unordered_map<std::type_index, TypeInfo> m_types;
    };
} // namespace Engine