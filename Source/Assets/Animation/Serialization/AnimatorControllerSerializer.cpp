#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\AnimatorController_generated.h"
#include "Assets\Animation\Serialization\AnimatorControllerSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        AssetGuid toFlat(const AssetGUID& value) { return { value.high, value.low }; }

        bool isConditionCompatible(const Engine::AnimatorParameterType type, const Engine::AnimatorConditionMode mode)
        {
            switch (type)
            {
            case Engine::AnimatorParameterType::Float:
            case Engine::AnimatorParameterType::Int:
                return mode == Engine::AnimatorConditionMode::Greater || mode == Engine::AnimatorConditionMode::Less
                    || mode == Engine::AnimatorConditionMode::Equals || mode == Engine::AnimatorConditionMode::NotEqual;
            case Engine::AnimatorParameterType::Bool:
                return mode == Engine::AnimatorConditionMode::If || mode == Engine::AnimatorConditionMode::IfNot;
            case Engine::AnimatorParameterType::Trigger:
                return mode == Engine::AnimatorConditionMode::Triggered;
            }
            return false;
        }

        bool validate(const Engine::AnimatorControllerAsset& controller)
        {
            if (!controller.guid.isValid() || !controller.skeletonGuid.isValid()
                || !controller.skeletonSignature.isValid() || controller.layerId == 0 || controller.layerName.empty()
                || controller.defaultState == 0 || controller.states.empty())
                return false;

            std::unordered_map<Engine::AnimatorParameterID, Engine::AnimatorParameterType> parameterTypes;
            std::unordered_set<std::string> parameterNames;
            parameterTypes.reserve(controller.parameters.size());
            for (const Engine::AnimatorParameter& parameter : controller.parameters)
            {
                if (parameter.id == 0 || parameter.name.empty() || !std::isfinite(parameter.defaultFloat)
                    || !parameterTypes.emplace(parameter.id, parameter.type).second
                    || !parameterNames.emplace(parameter.name).second)
                    return false;
            }

            std::unordered_set<Engine::AnimatorStateID> stateIds;
            std::unordered_set<std::string> stateNames;
            stateIds.reserve(controller.states.size());
            for (const Engine::AnimatorState& state : controller.states)
            {
                if (state.id == 0 || state.name.empty() || !state.clipGuid.isValid() || !std::isfinite(state.speed)
                    || !stateIds.emplace(state.id).second || !stateNames.emplace(state.name).second)
                    return false;
            }
            if (!stateIds.contains(controller.defaultState))
                return false;

            std::unordered_set<Engine::AnimatorTransitionID> transitionIds;
            for (const Engine::AnimatorTransition& transition : controller.transitions)
            {
                if (transition.id == 0 || !transitionIds.emplace(transition.id).second
                    || !stateIds.contains(transition.destinationState)
                    || (!transition.anyState && !stateIds.contains(transition.sourceState))
                    || !std::isfinite(transition.duration) || transition.duration < 0.0f
                    || !std::isfinite(transition.exitTime) || transition.exitTime < 0.0f)
                    return false;
                for (const Engine::AnimatorCondition& condition : transition.conditions)
                {
                    const auto parameter = parameterTypes.find(condition.parameterId);
                    if (parameter == parameterTypes.end() || !std::isfinite(condition.floatThreshold)
                        || !isConditionCompatible(parameter->second, condition.mode))
                        return false;
                }
            }
            return true;
        }
    }

    bool AnimatorControllerSerializer::save(const std::filesystem::path& path, const AnimatorControllerAsset& controller) const
    {
        if (!validate(controller))
            return false;

        flatbuffers::FlatBufferBuilder builder(1024);
        std::vector<flatbuffers::Offset<AnimatorParameterData>> parameters;
        parameters.reserve(controller.parameters.size());
        for (const Engine::AnimatorParameter& parameter : controller.parameters)
            parameters.push_back(CreateAnimatorParameterData(builder, parameter.id, builder.CreateString(parameter.name),
                static_cast<AnimatorParameterType>(parameter.type), parameter.defaultFloat, parameter.defaultInt,
                parameter.defaultBool));

        std::vector<flatbuffers::Offset<AnimatorStateData>> states;
        states.reserve(controller.states.size());
        for (const Engine::AnimatorState& state : controller.states)
        {
            const AssetGuid clipGuid = toFlat(state.clipGuid);
            states.push_back(CreateAnimatorStateData(builder, state.id, builder.CreateString(state.name), &clipGuid,
                state.speed, static_cast<AnimatorWrapOverride>(state.wrapOverride)));
        }

        std::vector<flatbuffers::Offset<AnimatorTransitionData>> transitions;
        transitions.reserve(controller.transitions.size());
        for (const Engine::AnimatorTransition& transition : controller.transitions)
        {
            std::vector<flatbuffers::Offset<AnimatorConditionData>> conditions;
            conditions.reserve(transition.conditions.size());
            for (const Engine::AnimatorCondition& condition : transition.conditions)
                conditions.push_back(CreateAnimatorConditionData(builder, condition.parameterId,
                    static_cast<AnimatorConditionMode>(condition.mode), condition.floatThreshold,
                    condition.intThreshold));
            transitions.push_back(CreateAnimatorTransitionData(builder, transition.id, transition.sourceState,
                transition.destinationState, transition.duration, transition.exitTime, transition.hasExitTime,
                transition.anyState, builder.CreateVector(conditions)));
        }

        const AssetGuid guid = toFlat(controller.guid);
        const AssetGuid skeletonGuid = toFlat(controller.skeletonGuid);
        const auto data = CreateAnimatorControllerAssetData(builder, &guid, builder.CreateString(controller.name),
            &skeletonGuid, controller.skeletonSignature.high, controller.skeletonSignature.low,
            controller.layerId, builder.CreateString(controller.layerName), controller.defaultState,
            builder.CreateVector(parameters), builder.CreateVector(states),
            builder.CreateVector(transitions), builder.CreateString(controller.sourcePath.generic_string()));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_ANIMATOR_CONTROLLER_VERSION, 0);
        FinishAnimatorControllerFileBuffer(builder, CreateAnimatorControllerFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool AnimatorControllerSerializer::load(const std::filesystem::path& path, AnimatorControllerAsset& controller) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("ACTR"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyAnimatorControllerFileBuffer(verifier))
            return false;
        const AnimatorControllerFile* file = GetAnimatorControllerFile(reader.data());
        const AnimatorControllerAssetData* source = file ? file->controller() : nullptr;
        if (file == nullptr || file->header() == nullptr || source == nullptr || source->guid() == nullptr
            || source->skeleton_guid() == nullptr || source->name() == nullptr
            || source->parameters() == nullptr || source->states() == nullptr || source->transitions() == nullptr
            || file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() != CURRENT_ANIMATOR_CONTROLLER_VERSION)
            return false;

        AnimatorControllerAsset loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        loaded.skeletonGuid = { source->skeleton_guid()->high(), source->skeleton_guid()->low() };
        loaded.skeletonSignature = { source->signature_high(), source->signature_low() };
        loaded.layerId = source->layer_id();
        loaded.layerName = source->layer_name() ? source->layer_name()->str() : "";
        loaded.defaultState = source->default_state();
        loaded.sourcePath = source->source_path() ? source->source_path()->str() : "";

        loaded.parameters.reserve(source->parameters()->size());
        for (const AnimatorParameterData* parameter : *source->parameters())
        {
            if (parameter == nullptr || parameter->name() == nullptr || parameter->type() > AnimatorParameterType_Trigger)
                return false;
            loaded.parameters.push_back({ parameter->id(), parameter->name()->str(),
                static_cast<Engine::AnimatorParameterType>(parameter->type()), parameter->default_float(),
                parameter->default_int(), parameter->default_bool() });
        }
        loaded.states.reserve(source->states()->size());
        for (const AnimatorStateData* state : *source->states())
        {
            if (state == nullptr || state->name() == nullptr || state->clip_guid() == nullptr
                || state->wrap_override() > AnimatorWrapOverride_PingPong)
                return false;
            loaded.states.push_back({ state->id(), state->name()->str(),
                { state->clip_guid()->high(), state->clip_guid()->low() }, state->speed(),
                static_cast<Engine::AnimatorWrapOverride>(state->wrap_override()) });
        }
        loaded.transitions.reserve(source->transitions()->size());
        for (const AnimatorTransitionData* transition : *source->transitions())
        {
            if (transition == nullptr || transition->conditions() == nullptr)
                return false;
            Engine::AnimatorTransition loadedTransition{ transition->id(), transition->source_state(), transition->destination_state(),
                transition->duration(), transition->exit_time(), transition->has_exit_time(), transition->any_state() };
            loadedTransition.conditions.reserve(transition->conditions()->size());
            for (const AnimatorConditionData* condition : *transition->conditions())
            {
                if (condition == nullptr || condition->mode() > AnimatorConditionMode_Triggered)
                    return false;
                loadedTransition.conditions.push_back({ condition->parameter_id(),
                    static_cast<Engine::AnimatorConditionMode>(condition->mode()), condition->float_threshold(),
                    condition->int_threshold() });
            }
            loaded.transitions.push_back(std::move(loadedTransition));
        }
        if (!validate(loaded))
            return false;
        controller = std::move(loaded);
        return true;
    }
}