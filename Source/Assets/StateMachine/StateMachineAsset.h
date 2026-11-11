#pragma once

#include "Core\Object\ObjectGUID.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine
{
    using StateMachineID = std::uint32_t;
    using StateMachineParameterID = std::uint32_t;
    using StateMachineTransitionID = std::uint32_t;

    inline constexpr std::size_t STATE_MACHINE_MAX_NAME_LENGTH = 127;
    inline constexpr std::size_t STATE_MACHINE_MAX_EVENT_NAME_LENGTH = 256;
    inline constexpr std::size_t STATE_MACHINE_MAX_ACTION_PAYLOAD_SIZE = 64 * 1024;

    /** @brief Types supported by a State Machine parameter. */
    enum class StateMachineParameterType : std::uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger,
    };

    /** @brief Comparison operations available to a State Machine transition. */
    enum class StateMachineConditionMode : std::uint8_t
    {
        Greater,
        Less,
        Equals,
        NotEqual,
        If,
        IfNot,
        Triggered,
    };

    /** @brief Built-in actions executed when entering or leaving a state. */
    enum class StateMachineActionType : std::uint8_t
    {
        EmitEvent,
        PlayAnimationClip,
        Custom,
    };

    /** @brief Authored parameter definition; runtime values belong to each component instance. */
    struct StateMachineParameter
    {
        StateMachineParameterID id = 0;
        std::string name;
        StateMachineParameterType type = StateMachineParameterType::Float;
        float defaultFloat = 0.0f;
        std::int32_t defaultInt = 0;
        bool defaultBool = false;
    };

    /** @brief A typed entry/exit action stored in the shared asset. */
    struct StateMachineAction
    {
        StateMachineActionType type = StateMachineActionType::EmitEvent;
        std::string eventName;
        ObjectGUID animationClipGuid;
        std::filesystem::path animationClipPath;
        std::string customType;
        std::vector<std::uint8_t> payload;
    };

    /** @brief One parameter predicate in a transition. */
    struct StateMachineCondition
    {
        StateMachineParameterID parameterId = 0;
        StateMachineConditionMode mode = StateMachineConditionMode::Equals;
        float floatThreshold = 0.0f;
        std::int32_t intThreshold = 0;
    };

    /** @brief Authored state and its editor canvas position. */
    struct StateMachineState
    {
        StateMachineID id = 0;
        std::string name;
        float positionX = 0.0f;
        float positionY = 0.0f;
        std::vector<StateMachineAction> onEnter;
        std::vector<StateMachineAction> onExit;
    };

    /** @brief Directed transition; the first matching transition in asset order wins. */
    struct StateMachineTransition
    {
        StateMachineTransitionID id = 0;
        StateMachineID sourceState = 0;
        StateMachineID destinationState = 0;
        std::int32_t priority = 0;
        std::string eventName;
        std::vector<StateMachineCondition> conditions;
    };

    /** @brief Immutable shared graph definition. Runtime state is never stored here. */
    struct StateMachineAsset
    {
        ObjectGUID guid;
        std::string name = "State Machine";
        StateMachineID defaultState = 0;
        std::vector<StateMachineParameter> parameters;
        std::vector<StateMachineState> states;
        std::vector<StateMachineTransition> transitions;
    };

    /** @brief Generation-checked State Machine asset reference. */
    struct StateMachineAssetHandle
    {
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;
        std::uint32_t index = INVALID_INDEX;
        std::uint32_t generation = 0;

        /** @brief Whether the handle is structurally valid; the manager also checks its generation. */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }
        friend bool operator==(const StateMachineAssetHandle&, const StateMachineAssetHandle&) = default;
    };

    /** @brief Validates the complete authored graph before saving or publishing it. */
    bool validateStateMachineAsset(const StateMachineAsset& asset);
}
