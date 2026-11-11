#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Assets\StateMachine\Serialization\StateMachineAssetSerializer.h"
#include "Core\Containers\String.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\StateMachineAsset_generated.h"

namespace Engine::Serialization
{
    namespace
    {
        AssetGuid toFlat(const ObjectGUID& value)
        {
            return { value.high, value.low };
        }

        bool readAction(const StateMachineActionData* source, StateMachineAction& destination)
        {
            if (source == nullptr || source->type() > StateMachineActionType_Custom)
                return false;
            if ((source->event_name() != nullptr
                    && source->event_name()->size() > STATE_MACHINE_MAX_EVENT_NAME_LENGTH)
                || (source->custom_type() != nullptr
                    && source->custom_type()->size() > STATE_MACHINE_MAX_NAME_LENGTH)
                || (source->animation_clip_path() != nullptr && source->animation_clip_path()->size() > 32767)
                || (source->payload() != nullptr
                    && source->payload()->size() > STATE_MACHINE_MAX_ACTION_PAYLOAD_SIZE))
            {
                LOG_ERROR("[StateMachine] State action exceeds a serialized field limit.");
                return false;
            }
            StateMachineAction action;
            action.type = static_cast<Engine::StateMachineActionType>(source->type());
            if (source->event_name() != nullptr)
                action.eventName = source->event_name()->str();
            if (source->clip_guid() != nullptr)
                action.animationClipGuid = { source->clip_guid()->high(), source->clip_guid()->low() };
            if (source->animation_clip_path() != nullptr)
                action.animationClipPath = String(source->animation_clip_path()->string_view()).toWide();
            if (source->custom_type() != nullptr)
                action.customType = source->custom_type()->str();
            if (source->payload() != nullptr)
                action.payload.assign(source->payload()->begin(), source->payload()->end());
            destination = std::move(action);
            return true;
        }
    }

    bool StateMachineAssetSerializer::save(const std::filesystem::path& path,
        const Engine::StateMachineAsset& asset) const
    {
        if (!Engine::validateStateMachineAsset(asset))
            return false;
        const auto validActionPaths = [](const std::vector<Engine::StateMachineAction>& actions)
        {
            for (const Engine::StateMachineAction& action : actions)
            {
                if (action.type == Engine::StateMachineActionType::PlayAnimationClip
                    && String::fromWide(action.animationClipPath.c_str()).size() == 0)
                {
                    LOG_ERROR("[StateMachine] Animation Clip path cannot be encoded as UTF-8.");
                    return false;
                }
            }
            return true;
        };
        for (const Engine::StateMachineState& state : asset.states)
            if (!validActionPaths(state.onEnter) || !validActionPaths(state.onExit))
                return false;

        flatbuffers::FlatBufferBuilder builder(1024);
        std::vector<flatbuffers::Offset<StateMachineParameterData>> parameters;
        parameters.reserve(asset.parameters.size());
        for (const Engine::StateMachineParameter& parameter : asset.parameters)
        {
            parameters.push_back(CreateStateMachineParameterData(builder, parameter.id,
                builder.CreateString(parameter.name), static_cast<StateMachineParameterType>(parameter.type),
                parameter.defaultFloat, parameter.defaultInt, parameter.defaultBool));
        }

        const auto serializeActions = [&builder](const std::vector<Engine::StateMachineAction>& actions)
        {
            std::vector<flatbuffers::Offset<StateMachineActionData>> result;
            result.reserve(actions.size());
            for (const Engine::StateMachineAction& action : actions)
            {
                const AssetGuid clipGuid = toFlat(action.animationClipGuid);
                const auto clipPath = String::fromWide(action.animationClipPath.c_str());
                result.push_back(CreateStateMachineActionData(builder,
                    static_cast<StateMachineActionType>(action.type), builder.CreateString(action.eventName),
                    &clipGuid, builder.CreateString(clipPath.view()), builder.CreateString(action.customType),
                    builder.CreateVector(action.payload)));
            }
            return builder.CreateVector(result);
        };

        std::vector<flatbuffers::Offset<StateMachineStateData>> states;
        states.reserve(asset.states.size());
        for (const Engine::StateMachineState& state : asset.states)
        {
            states.push_back(CreateStateMachineStateData(builder, state.id, builder.CreateString(state.name),
                state.positionX, state.positionY, serializeActions(state.onEnter), serializeActions(state.onExit)));
        }

        std::vector<flatbuffers::Offset<StateMachineTransitionData>> transitions;
        transitions.reserve(asset.transitions.size());
        for (const Engine::StateMachineTransition& transition : asset.transitions)
        {
            std::vector<flatbuffers::Offset<StateMachineConditionData>> conditions;
            conditions.reserve(transition.conditions.size());
            for (const Engine::StateMachineCondition& condition : transition.conditions)
            {
                conditions.push_back(CreateStateMachineConditionData(builder, condition.parameterId,
                    static_cast<StateMachineConditionMode>(condition.mode), condition.floatThreshold,
                    condition.intThreshold));
            }
            transitions.push_back(CreateStateMachineTransitionData(builder, transition.id,
                transition.sourceState, transition.destinationState, transition.priority,
                builder.CreateString(transition.eventName), builder.CreateVector(conditions)));
        }

        const AssetGuid guid = toFlat(asset.guid);
        const auto data = CreateStateMachineAssetData(builder, &guid, builder.CreateString(asset.name),
            asset.defaultState, builder.CreateVector(parameters), builder.CreateVector(states),
            builder.CreateVector(transitions));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_STATE_MACHINE_ASSET_VERSION, 0);
        FinishStateMachineAssetFileBuffer(builder, CreateStateMachineAssetFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool StateMachineAssetSerializer::load(const std::filesystem::path& path,
        Engine::StateMachineAsset& asset) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("SMAS"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyStateMachineAssetFileBuffer(verifier))
            return false;

        const StateMachineAssetFile* const file = GetStateMachineAssetFile(reader.data());
        const StateMachineAssetData* const source = file != nullptr ? file->asset() : nullptr;
        if (file == nullptr || file->header() == nullptr || source == nullptr || source->guid() == nullptr
            || source->name() == nullptr || source->parameters() == nullptr || source->states() == nullptr
            || source->transitions() == nullptr
            || source->name()->size() > STATE_MACHINE_MAX_NAME_LENGTH
            || source->parameters()->size() > 65536 || source->states()->size() > 65536
            || source->transitions()->size() > 262144
            || file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() != CURRENT_STATE_MACHINE_ASSET_VERSION)
        {
            LOG_ERROR("[StateMachine] Invalid State Machine asset header or required data.");
            return false;
        }

        Engine::StateMachineAsset loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        loaded.defaultState = source->default_state();

        loaded.parameters.reserve(source->parameters()->size());
        for (const StateMachineParameterData* const parameter : *source->parameters())
        {
            if (parameter == nullptr || parameter->name() == nullptr
                || parameter->name()->size() > STATE_MACHINE_MAX_NAME_LENGTH
                || parameter->type() > StateMachineParameterType_Trigger)
            {
                LOG_ERROR("[StateMachine] Invalid State Machine parameter data.");
                return false;
            }
            loaded.parameters.push_back({ parameter->id(), parameter->name()->str(),
                static_cast<Engine::StateMachineParameterType>(parameter->type()),
                parameter->default_float(), parameter->default_int(), parameter->default_bool() });
        }

        loaded.states.reserve(source->states()->size());
        for (const StateMachineStateData* const state : *source->states())
        {
            if (state == nullptr || state->name() == nullptr || state->on_enter() == nullptr
                || state->name()->size() > STATE_MACHINE_MAX_NAME_LENGTH
                || state->on_exit() == nullptr)
            {
                LOG_ERROR("[StateMachine] Invalid State Machine state data.");
                return false;
            }
            Engine::StateMachineState loadedState{ state->id(), state->name()->str(),
                state->position_x(), state->position_y() };
            loadedState.onEnter.reserve(state->on_enter()->size());
            for (const StateMachineActionData* const action : *state->on_enter())
            {
                Engine::StateMachineAction loadedAction;
                if (!readAction(action, loadedAction))
                    return false;
                loadedState.onEnter.push_back(std::move(loadedAction));
            }
            loadedState.onExit.reserve(state->on_exit()->size());
            for (const StateMachineActionData* const action : *state->on_exit())
            {
                Engine::StateMachineAction loadedAction;
                if (!readAction(action, loadedAction))
                    return false;
                loadedState.onExit.push_back(std::move(loadedAction));
            }
            loaded.states.push_back(std::move(loadedState));
        }

        loaded.transitions.reserve(source->transitions()->size());
        for (const StateMachineTransitionData* const transition : *source->transitions())
        {
            if (transition == nullptr || transition->event_name() == nullptr || transition->conditions() == nullptr)
            {
                LOG_ERROR("[StateMachine] Invalid State Machine transition data.");
                return false;
            }
            if (transition->event_name()->size() > STATE_MACHINE_MAX_EVENT_NAME_LENGTH)
            {
                LOG_ERROR("[StateMachine] Transition event name exceeds its serialized field limit.");
                return false;
            }
            Engine::StateMachineTransition loadedTransition{ transition->id(), transition->source_state(),
                transition->destination_state(), transition->priority(), transition->event_name()->str() };
            loadedTransition.conditions.reserve(transition->conditions()->size());
            for (const StateMachineConditionData* const condition : *transition->conditions())
            {
                if (condition == nullptr || condition->mode() > StateMachineConditionMode_Triggered)
                {
                    LOG_ERROR("[StateMachine] Invalid State Machine condition data.");
                    return false;
                }
                loadedTransition.conditions.push_back({ condition->parameter_id(),
                    static_cast<Engine::StateMachineConditionMode>(condition->mode()),
                    condition->float_threshold(), condition->int_threshold() });
            }
            loaded.transitions.push_back(std::move(loadedTransition));
        }

        if (!Engine::validateStateMachineAsset(loaded))
            return false;
        asset = std::move(loaded);
        return true;
    }
}
