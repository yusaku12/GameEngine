#pragma once

#include <cstdint>
#include <string_view>
#include <typeindex>
#include <unordered_map>

#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"

namespace Engine
{
    using ComponentTypeID = std::uint32_t;

    /**
     * @brief Component型のEditor/runtime向けメタデータ。
     */
    struct ComponentTypeInfo
    {
        ComponentTypeID id = 0;
        std::string_view name;
        bool allowMultiple = false;
        bool required = false;
        bool executeLifecycle = false;
    };

    /**
     * @brief Component型とメタデータの対応を管理するRegistry。
     * @thread_safety Main thread only.
     */
    class ComponentRegistry
    {
    public:
        static ComponentRegistry& instance() noexcept;

        ComponentRegistry() = default;
        GE_DISABLE_COPY_AND_MOVE(ComponentRegistry);

        template <typename T>
        ComponentTypeID registerType(std::string_view name, const bool allowMultiple = false,
            const bool required = false, const bool executeLifecycle = false)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const std::type_index type = typeid(T);
            if (const auto found = m_types.find(type); found != m_types.end())
                return found->second.id;

            const ComponentTypeInfo info{
                .id = m_nextId++,
                .name = name,
                .allowMultiple = allowMultiple,
                .required = required,
                .executeLifecycle = executeLifecycle
            };
            m_types.emplace(type, info);
            m_ids.emplace(info.id, type);
            return info.id;
        }

        template <typename T>
        ComponentTypeID ensureRegistered()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            if (const auto found = m_types.find(std::type_index(typeid(T))); found != m_types.end())
                return found->second.id;
            return registerType<T>(typeid(T).name());
        }

        template <typename T>
        [[nodiscard]] const ComponentTypeInfo* get() const noexcept
        {
            const auto found = m_types.find(std::type_index(typeid(T)));
            return found == m_types.end() ? nullptr : &found->second;
        }

        [[nodiscard]] const ComponentTypeInfo* get(ComponentTypeID id) const noexcept;
        [[nodiscard]] const ComponentTypeInfo* get(std::type_index type) const noexcept;
        [[nodiscard]] const ComponentTypeInfo* findByName(std::string_view name) const noexcept;
        void clear() noexcept;

    private:
        ComponentTypeID m_nextId = 1;
        std::unordered_map<std::type_index, ComponentTypeInfo> m_types;
        std::unordered_map<ComponentTypeID, std::type_index> m_ids;
    };
} // namespace Engine