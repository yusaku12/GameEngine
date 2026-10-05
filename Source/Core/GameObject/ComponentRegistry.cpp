#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\GameObject\ComponentRegistry.h"

namespace Engine
{
    ComponentRegistry& ComponentRegistry::instance() noexcept
    {
        static ComponentRegistry registry;
        return registry;
    }

    ComponentTypeID ComponentRegistry::registerTypeInfo(const std::type_index type, const std::string_view name,
        const ComponentTypeInfo::Factory factory, const bool allowMultiple, const bool required,
        const bool executeLifecycle)
    {
        if (const auto found = m_types.find(type); found != m_types.end())
            return found->second.id;
        if (name.empty() || factory == nullptr)
        {
            LOG_ERROR("Cannot register a Component type with an empty name or null factory.");
            return 0;
        }
        if (findByName(name) != nullptr)
        {
            LOG_ERROR("Cannot register Component type because the name '{}' is already in use.", name);
            return 0;
        }
        if (m_nextId == 0)
        {
            LOG_ERROR("Cannot register Component type because ComponentTypeID values are exhausted.");
            return 0;
        }

        const ComponentTypeID id = m_nextId;
        const ComponentTypeInfo info{
            .id = id,
            .name = std::string(name),
            .factory = factory,
            .allowMultiple = allowMultiple,
            .required = required,
            .executeLifecycle = executeLifecycle
        };
        try
        {
            const auto [typeEntry, typeInserted] = m_types.emplace(type, info);
            if (!typeInserted)
                return typeEntry->second.id;
            bool idInserted = false;
            try
            {
                const auto [idEntry, inserted] = m_ids.emplace(id, type);
                GE_UNUSED(idEntry);
                idInserted = inserted;
            }
            catch (const std::bad_alloc&)
            {
                m_types.erase(typeEntry);
                throw;
            }
            catch (const std::length_error&)
            {
                m_types.erase(typeEntry);
                throw;
            }
            if (!idInserted)
            {
                m_types.erase(typeEntry);
                LOG_ERROR("Cannot register Component type because ComponentTypeID {} is already in use.", id);
                return 0;
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while registering Component type '{}'.", name);
            return 0;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Component registry reached its maximum capacity while registering '{}'.", name);
            return 0;
        }

        m_nextId = id == (std::numeric_limits<ComponentTypeID>::max)() ? 0 : id + 1;
        return id;
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

    std::unique_ptr<Component> ComponentRegistry::create(const std::string_view name) const
    {
        const ComponentTypeInfo* info = findByName(name);
        return info == nullptr || info->factory == nullptr ? nullptr : info->factory();
    }

    void ComponentRegistry::clear() noexcept
    {
        m_types.clear();
        m_ids.clear();
        m_nextId = 1;
    }
} // namespace Engine