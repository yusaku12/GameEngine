#include "Pch.h"
#include "StateMachine\StateMachineActionRegistry.h"
#include "Core\CoreDefines.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    StateMachineActionRegistry& StateMachineActionRegistry::instance() noexcept
    {
        static StateMachineActionRegistry registry;
        return registry;
    }

    bool StateMachineActionRegistry::registerType(const std::string_view name, const Validator validator,
        const Executor executor, const Editor editor)
    {
        if (name.empty() || name.size() > STATE_MACHINE_MAX_NAME_LENGTH
            || name.find('\0') != std::string_view::npos || executor == nullptr)
        {
            LOG_ERROR("[StateMachine] Custom action registration requires a valid name and executor.");
            return false;
        }
        try
        {
            const std::scoped_lock lock(m_mutex);
            const auto [entry, inserted] = m_types.emplace(std::string(name), Entry{ validator, executor, editor });
            GE_UNUSED(entry);
            if (!inserted)
            {
                LOG_ERROR("[StateMachine] Custom action type '{}' is already registered.", name);
                return false;
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to allocate custom action registration '{}'.", name);
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[StateMachine] Custom action registry capacity exceeded.");
        }
        return false;
    }

    bool StateMachineActionRegistry::validate(const StateMachineAction& action) const
    {
        Entry entry;
        {
            const std::scoped_lock lock(m_mutex);
            const auto found = m_types.find(action.customType);
            if (found == m_types.end() || found->second.validator == nullptr)
                return true;
            entry = found->second;
        }
        return entry.validator(action.payload);
    }

    bool StateMachineActionRegistry::execute(StateMachineComponent& component, const StateMachineAction& action) const
    {
        Entry entry;
        {
            const std::scoped_lock lock(m_mutex);
            const auto found = m_types.find(action.customType);
            if (found == m_types.end())
            {
                LOG_ERROR("[StateMachine] Custom action type '{}' is not registered.", action.customType);
                return false;
            }
            entry = found->second;
        }
        return entry.executor(component, action.payload);
    }

    bool StateMachineActionRegistry::drawEditor(StateMachineAction& action, bool& changed) const
    {
        Entry entry;
        {
            const std::scoped_lock lock(m_mutex);
            const auto found = m_types.find(action.customType);
            if (found == m_types.end() || found->second.editor == nullptr)
                return false;
            entry = found->second;
        }
        changed = entry.editor(action.payload);
        return true;
    }

    std::vector<std::string> StateMachineActionRegistry::getRegisteredTypes() const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<std::string> names;
        names.reserve(m_types.size());
        for (const auto& [name, entry] : m_types)
        {
            GE_UNUSED(entry);
            names.push_back(name);
        }
        std::ranges::sort(names);
        return names;
    }
}
