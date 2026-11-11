#pragma once

#include "Assets\StateMachine\StateMachineAsset.h"
#include <mutex>
#include <span>
#include <unordered_map>

namespace Engine
{
    class StateMachineComponent;

    /**
     * @brief Code-registered extension points for serialized State Machine actions.
     * @details Assets store only type names and payload bytes; function pointers remain process-local.
     * @thread_safety Registration and lookup are synchronized. Callbacks execute without the registry lock.
     */
    class StateMachineActionRegistry
    {
    public:
        using Validator = bool(*)(std::span<const std::uint8_t>);
        using Executor = bool(*)(StateMachineComponent&, std::span<const std::uint8_t>);
        using Editor = bool(*)(std::vector<std::uint8_t>&);

        /** @brief Returns the process-wide action type registry. */
        static StateMachineActionRegistry& instance() noexcept;

        /**
         * @brief Registers a custom action type. Duplicate names are rejected.
         * @param name Stable serialized action type name.
         * @param validator Validates serialized payload bytes.
         * @param executor Runs the action against one component instance.
         * @param editor Edits payload bytes in the graph editor; optional.
         * @return True when the type is registered.
         */
        bool registerType(std::string_view name, Validator validator, Executor executor, Editor editor = nullptr);

        /** @brief Validates known custom payloads; unknown types remain loadable for forward compatibility. */
        bool validate(const StateMachineAction& action) const;
        /** @brief Runs a registered custom action, returning false for unknown or failed actions. */
        bool execute(StateMachineComponent& component, const StateMachineAction& action) const;
        /** @brief Draws a registered custom editor; returns availability and reports payload changes separately. */
        bool drawEditor(StateMachineAction& action, bool& changed) const;
        /** @brief Copies registered type names in sorted order. */
        std::vector<std::string> getRegisteredTypes() const;

    private:
        struct Entry
        {
            Validator validator = nullptr;
            Executor executor = nullptr;
            Editor editor = nullptr;
        };

        mutable std::mutex m_mutex;
        std::unordered_map<std::string, Entry> m_types;
    };
}
