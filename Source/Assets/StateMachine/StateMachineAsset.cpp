#include "Pch.h"
#include "Assets\StateMachine\StateMachineAsset.h"
#include "Core\Logging\Logger.h"
#include "StateMachine\StateMachineActionRegistry.h"

namespace Engine
{
    namespace
    {
        bool isFinite(const float value) noexcept
        {
            return std::isfinite(value);
        }

        bool isConditionCompatible(const StateMachineParameterType type,
            const StateMachineConditionMode mode) noexcept
        {
            switch (type)
            {
            case StateMachineParameterType::Float:
            case StateMachineParameterType::Int:
                return mode == StateMachineConditionMode::Greater || mode == StateMachineConditionMode::Less
                    || mode == StateMachineConditionMode::Equals || mode == StateMachineConditionMode::NotEqual;
            case StateMachineParameterType::Bool:
                return mode == StateMachineConditionMode::If || mode == StateMachineConditionMode::IfNot;
            case StateMachineParameterType::Trigger:
                return mode == StateMachineConditionMode::Triggered;
            }
            return false;
        }

        bool validName(const std::string& value) noexcept
        {
            return !value.empty() && value.size() <= STATE_MACHINE_MAX_NAME_LENGTH
                && value.find('\0') == std::string::npos;
        }

        bool validEventName(const std::string& value) noexcept
        {
            return !value.empty() && value.size() <= STATE_MACHINE_MAX_EVENT_NAME_LENGTH
                && value.find('\0') == std::string::npos;
        }

        bool validateAction(const StateMachineAction& action)
        {
            switch (action.type)
            {
            case StateMachineActionType::EmitEvent:
                if (!validEventName(action.eventName) || action.animationClipGuid.isValid()
                    || !action.animationClipPath.empty() || !action.customType.empty() || !action.payload.empty())
                    break;
                return true;
            case StateMachineActionType::PlayAnimationClip:
                if (action.eventName.empty() && action.animationClipGuid.isValid() && !action.animationClipPath.empty()
                    && action.animationClipPath.native().size() <= 32767
                    && action.customType.empty() && action.payload.empty())
                    return true;
                break;
            case StateMachineActionType::Custom:
                if (validName(action.customType) && action.eventName.empty() && !action.animationClipGuid.isValid()
                    && action.animationClipPath.empty() && action.payload.size() <= STATE_MACHINE_MAX_ACTION_PAYLOAD_SIZE)
                    return StateMachineActionRegistry::instance().validate(action);
                break;
            }
            LOG_ERROR("[StateMachine] Invalid state action.");
            return false;
        }
    }

    bool validateStateMachineAsset(const StateMachineAsset& asset)
    {
        if (!asset.guid.isValid() || !validName(asset.name) || asset.states.empty()
            || asset.states.size() > 65536 || asset.parameters.size() > 65536
            || asset.transitions.size() > 262144)
        {
            LOG_ERROR("[StateMachine] Asset identity, name, or graph size is invalid.");
            return false;
        }

        std::unordered_map<StateMachineParameterID, StateMachineParameterType> parameterTypes;
        std::unordered_set<std::string> parameterNames;
        parameterTypes.reserve(asset.parameters.size());
        parameterNames.reserve(asset.parameters.size());
        for (const StateMachineParameter& parameter : asset.parameters)
        {
            if (parameter.id == 0 || !validName(parameter.name)
                || parameter.type > StateMachineParameterType::Trigger
                || (parameter.type == StateMachineParameterType::Float && !isFinite(parameter.defaultFloat))
                || !parameterTypes.emplace(parameter.id, parameter.type).second
                || !parameterNames.emplace(parameter.name).second)
            {
                LOG_ERROR("[StateMachine] Parameter IDs/names/default values must be valid and unique.");
                return false;
            }
        }

        std::unordered_set<StateMachineID> stateIds;
        std::unordered_set<std::string> stateNames;
        stateIds.reserve(asset.states.size());
        stateNames.reserve(asset.states.size());
        for (const StateMachineState& state : asset.states)
        {
            if (state.id == 0 || !validName(state.name) || !isFinite(state.positionX) || !isFinite(state.positionY)
                || !stateIds.emplace(state.id).second || !stateNames.emplace(state.name).second)
            {
                LOG_ERROR("[StateMachine] State IDs/names/positions must be valid and state IDs/names unique.");
                return false;
            }
            for (const StateMachineAction& action : state.onEnter)
                if (!validateAction(action))
                    return false;
            for (const StateMachineAction& action : state.onExit)
                if (!validateAction(action))
                    return false;
        }
        if (!stateIds.contains(asset.defaultState))
        {
            LOG_ERROR("[StateMachine] The default state does not exist.");
            return false;
        }

        std::unordered_set<StateMachineTransitionID> transitionIds;
        transitionIds.reserve(asset.transitions.size());
        for (const StateMachineTransition& transition : asset.transitions)
        {
            if (transition.id == 0 || !transitionIds.emplace(transition.id).second
                || !stateIds.contains(transition.sourceState) || !stateIds.contains(transition.destinationState)
                || transition.sourceState == transition.destinationState
                || transition.eventName.size() > STATE_MACHINE_MAX_EVENT_NAME_LENGTH
                || transition.eventName.find('\0') != std::string::npos)
            {
                LOG_ERROR("[StateMachine] Transition IDs and state references must be valid and unique.");
                return false;
            }
            for (const StateMachineCondition& condition : transition.conditions)
            {
                const auto parameter = parameterTypes.find(condition.parameterId);
                if (parameter == parameterTypes.end() || condition.mode > StateMachineConditionMode::Triggered
                    || (parameter->second == StateMachineParameterType::Float && !isFinite(condition.floatThreshold))
                    || !isConditionCompatible(parameter->second, condition.mode))
                {
                    LOG_ERROR("[StateMachine] Transition condition has an invalid parameter, mode, or threshold.");
                    return false;
                }
            }
        }
        return true;
    }
}
