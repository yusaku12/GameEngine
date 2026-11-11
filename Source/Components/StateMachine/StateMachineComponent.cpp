#include "Pch.h"
#include "Components\StateMachine\StateMachineComponent.h"
#include <flatbuffers/flatbuffers.h>
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\StateMachine\StateMachineAssetManager.h"
#include "Components\Animation\AnimatorComponent.h"
#include "Core\Containers\String.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\StateMachineComponent_generated.h"
#include "StateMachine\StateMachineActionRegistry.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        constexpr std::size_t MAX_QUEUED_EVENTS = 64;
        const auto stateMachineType = ComponentRegistry::instance().registerType<StateMachineComponent>(
            "State Machine", false, false, true);

        const Serialization::StateMachineComponentPayload* readPayload(const std::uint32_t version,
            const std::span<const std::uint8_t> payload)
        {
            flatbuffers::Verifier verifier(payload.data(), payload.size());
            if (version != Serialization::CURRENT_STATE_MACHINE_COMPONENT_VERSION || payload.size() < 8
                || !Serialization::VerifyStateMachineComponentPayloadBuffer(verifier))
            {
                LOG_ERROR("[StateMachine] Invalid State Machine component payload/version.");
                return nullptr;
            }
            return Serialization::GetStateMachineComponentPayload(payload.data());
        }

        bool validEventName(const std::string_view eventName) noexcept
        {
            return !eventName.empty() && eventName.size() <= STATE_MACHINE_MAX_EVENT_NAME_LENGTH
                && eventName.find('\0') == std::string_view::npos;
        }
    }

    StateMachineComponent::StateMachineComponent() noexcept
    {
        GE_UNUSED(stateMachineType);
    }

    bool StateMachineComponent::setStateMachine(const StateMachineAssetHandle handle)
    {
        std::shared_ptr<const StateMachineAsset> asset;
        std::vector<RuntimeParameter> parameters;
        if (handle.isValid())
        {
            asset = StateMachineAssetManager::instance().get(handle);
            if (!asset)
            {
                LOG_ERROR("[StateMachine] Cannot assign a stale State Machine asset handle.");
                return false;
            }
            try
            {
                parameters.reserve(asset->parameters.size());
                for (const StateMachineParameter& definition : asset->parameters)
                    parameters.push_back({ definition.id, definition.type, definition.defaultFloat,
                        definition.defaultInt, definition.defaultBool });
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[StateMachine] Failed to allocate instance parameters.");
                return false;
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[StateMachine] Instance parameter capacity exceeded.");
                return false;
            }
        }
        m_assetHandle = handle;
        m_asset = std::move(asset);
        m_parameters = std::move(parameters);
        m_startingState = 0;
        m_currentState = 0;
        m_runtimeStarted = false;
        m_missingAssetReported = false;
        m_pendingEvents.clear();
        m_emittedEvents.clear();
        return true;
    }

    bool StateMachineComponent::setStartingState(const StateMachineID stateId)
    {
        if (stateId != 0 && findState(stateId) == nullptr)
        {
            LOG_ERROR("[StateMachine] Starting state does not exist in the assigned asset.");
            return false;
        }
        m_startingState = stateId;
        m_currentState = 0;
        m_runtimeStarted = false;
        return true;
    }

    std::string_view StateMachineComponent::getCurrentStateName() const noexcept
    {
        const StateMachineState* const state = findState(m_currentState);
        return state != nullptr ? std::string_view(state->name) : std::string_view{};
    }

    StateMachineComponent::RuntimeParameter* StateMachineComponent::findParameter(const StateMachineParameterID id) noexcept
    {
        const auto found = std::ranges::find(m_parameters, id, &RuntimeParameter::id);
        return found != m_parameters.end() ? &*found : nullptr;
    }

    const StateMachineComponent::RuntimeParameter* StateMachineComponent::findParameter(
        const StateMachineParameterID id) const noexcept
    {
        const auto found = std::ranges::find(m_parameters, id, &RuntimeParameter::id);
        return found != m_parameters.end() ? &*found : nullptr;
    }

    const StateMachineState* StateMachineComponent::findState(const StateMachineID id) const noexcept
    {
        if (m_asset == nullptr || id == 0)
            return nullptr;
        const auto found = std::ranges::find(m_asset->states, id, &StateMachineState::id);
        return found != m_asset->states.end() ? &*found : nullptr;
    }

    bool StateMachineComponent::refreshAsset()
    {
        if (!m_assetHandle.isValid())
            return m_asset == nullptr;
        const auto latest = StateMachineAssetManager::instance().get(m_assetHandle);
        if (!latest)
        {
            if (!m_missingAssetReported)
            {
                LOG_ERROR("[StateMachine] Assigned asset handle is no longer registered.");
                m_missingAssetReported = true;
            }
            return false;
        }
        if (latest == m_asset)
            return true;

        std::vector<RuntimeParameter> updatedParameters;
        try
        {
            updatedParameters.reserve(latest->parameters.size());
            for (const StateMachineParameter& definition : latest->parameters)
            {
                RuntimeParameter value{ definition.id, definition.type, definition.defaultFloat,
                    definition.defaultInt, definition.defaultBool };
                const RuntimeParameter* const previous = findParameter(definition.id);
                if (previous != nullptr && previous->type == definition.type)
                {
                    value.floatValue = previous->floatValue;
                    value.intValue = previous->intValue;
                    value.boolValue = previous->boolValue;
                }
                updatedParameters.push_back(value);
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to refresh instance parameters after an asset edit.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[StateMachine] Refreshed parameter list exceeded its capacity.");
            return false;
        }

        m_asset = latest;
        m_parameters = std::move(updatedParameters);
        if (m_currentState != 0 && findState(m_currentState) == nullptr)
        {
            m_currentState = 0;
            m_runtimeStarted = false;
        }
        if (m_startingState != 0 && findState(m_startingState) == nullptr)
        {
            LOG_WARNING("[StateMachine] Start-state override was removed; using the asset default.");
            m_startingState = 0;
        }
        m_missingAssetReported = false;
        return true;
    }

    bool StateMachineComponent::setFloat(const StateMachineParameterID id, const float value) noexcept
    {
        RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Float || !std::isfinite(value))
        {
            LOG_ERROR("[StateMachine] Float parameter ID/type/value is invalid.");
            return false;
        }
        parameter->floatValue = value;
        return true;
    }

    bool StateMachineComponent::setInt(const StateMachineParameterID id, const std::int32_t value) noexcept
    {
        RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Int)
        {
            LOG_ERROR("[StateMachine] Int parameter ID/type is invalid.");
            return false;
        }
        parameter->intValue = value;
        return true;
    }

    bool StateMachineComponent::setBool(const StateMachineParameterID id, const bool value) noexcept
    {
        RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Bool)
        {
            LOG_ERROR("[StateMachine] Bool parameter ID/type is invalid.");
            return false;
        }
        parameter->boolValue = value;
        return true;
    }

    bool StateMachineComponent::setTrigger(const StateMachineParameterID id) noexcept
    {
        RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Trigger)
        {
            LOG_ERROR("[StateMachine] Trigger parameter ID/type is invalid.");
            return false;
        }
        parameter->boolValue = true;
        return true;
    }

    bool StateMachineComponent::resetTrigger(const StateMachineParameterID id) noexcept
    {
        RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Trigger)
        {
            LOG_ERROR("[StateMachine] Trigger parameter ID/type is invalid.");
            return false;
        }
        parameter->boolValue = false;
        return true;
    }

    bool StateMachineComponent::getFloat(const StateMachineParameterID id, float& value) const noexcept
    {
        const RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Float)
            return false;
        value = parameter->floatValue;
        return true;
    }

    bool StateMachineComponent::getInt(const StateMachineParameterID id, std::int32_t& value) const noexcept
    {
        const RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || parameter->type != StateMachineParameterType::Int)
            return false;
        value = parameter->intValue;
        return true;
    }

    bool StateMachineComponent::getBool(const StateMachineParameterID id, bool& value) const noexcept
    {
        const RuntimeParameter* const parameter = findParameter(id);
        if (parameter == nullptr || (parameter->type != StateMachineParameterType::Bool
            && parameter->type != StateMachineParameterType::Trigger))
            return false;
        value = parameter->boolValue;
        return true;
    }

    bool StateMachineComponent::sendEvent(const std::string_view eventName)
    {
        if (!validEventName(eventName) || m_pendingEvents.size() >= MAX_QUEUED_EVENTS)
        {
            LOG_ERROR("[StateMachine] Input event name is invalid or the event queue is full.");
            return false;
        }
        try
        {
            m_pendingEvents.emplace_back(eventName);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to queue input event.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[StateMachine] Input event queue capacity exceeded.");
        }
        return false;
    }

    bool StateMachineComponent::consumeEvent(const std::string_view eventName)
    {
        const auto found = std::ranges::find(m_emittedEvents, eventName);
        if (found == m_emittedEvents.end())
            return false;
        m_emittedEvents.erase(found);
        return true;
    }

    bool StateMachineComponent::emitEvent(const std::string_view eventName)
    {
        if (!validEventName(eventName) || m_emittedEvents.size() >= MAX_QUEUED_EVENTS)
        {
            LOG_ERROR("[StateMachine] Output event name is invalid or the event queue is full.");
            return false;
        }
        try
        {
            m_emittedEvents.emplace_back(eventName);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to queue output event.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[StateMachine] Output event queue capacity exceeded.");
        }
        return false;
    }

    void StateMachineComponent::reset() noexcept
    {
        m_currentState = 0;
        m_runtimeStarted = false;
        m_pendingEvents.clear();
        m_emittedEvents.clear();
        if (m_asset == nullptr)
        {
            m_parameters.clear();
            return;
        }
        for (RuntimeParameter& parameter : m_parameters)
        {
            const auto definition = std::ranges::find(m_asset->parameters, parameter.id, &StateMachineParameter::id);
            if (definition == m_asset->parameters.end())
                continue;
            parameter.floatValue = definition->defaultFloat;
            parameter.intValue = definition->defaultInt;
            parameter.boolValue = definition->defaultBool;
        }
    }

    bool StateMachineComponent::conditionsPass(const StateMachineTransition& transition) const noexcept
    {
        if (!transition.eventName.empty()
            && std::ranges::find(m_pendingEvents, transition.eventName) == m_pendingEvents.end())
            return false;
        for (const StateMachineCondition& condition : transition.conditions)
        {
            const RuntimeParameter* const parameter = findParameter(condition.parameterId);
            if (parameter == nullptr)
                return false;
            bool passed = false;
            switch (condition.mode)
            {
            case StateMachineConditionMode::Greater:
                passed = parameter->type == StateMachineParameterType::Float
                    ? parameter->floatValue > condition.floatThreshold
                    : parameter->intValue > condition.intThreshold;
                break;
            case StateMachineConditionMode::Less:
                passed = parameter->type == StateMachineParameterType::Float
                    ? parameter->floatValue < condition.floatThreshold
                    : parameter->intValue < condition.intThreshold;
                break;
            case StateMachineConditionMode::Equals:
                passed = parameter->type == StateMachineParameterType::Float
                    ? std::abs(parameter->floatValue - condition.floatThreshold) <= 0.00001f
                    : parameter->intValue == condition.intThreshold;
                break;
            case StateMachineConditionMode::NotEqual:
                passed = parameter->type == StateMachineParameterType::Float
                    ? std::abs(parameter->floatValue - condition.floatThreshold) > 0.00001f
                    : parameter->intValue != condition.intThreshold;
                break;
            case StateMachineConditionMode::If:
                passed = parameter->type == StateMachineParameterType::Bool && parameter->boolValue;
                break;
            case StateMachineConditionMode::IfNot:
                passed = parameter->type == StateMachineParameterType::Bool && !parameter->boolValue;
                break;
            case StateMachineConditionMode::Triggered:
                passed = parameter->type == StateMachineParameterType::Trigger && parameter->boolValue;
                break;
            }
            if (!passed)
                return false;
        }
        return true;
    }

    void StateMachineComponent::updateStateMachine()
    {
        const std::shared_ptr<const StateMachineAsset> asset = m_asset;
        if (asset == nullptr)
        {
            if (m_assetHandle.isValid() && !m_missingAssetReported)
            {
                LOG_ERROR("[StateMachine] Assigned asset snapshot is unavailable.");
                m_missingAssetReported = true;
            }
            return;
        }
        m_missingAssetReported = false;

        if (!m_runtimeStarted)
        {
            const StateMachineID initialStateId = m_startingState != 0 ? m_startingState : asset->defaultState;
            const StateMachineState* const initialState = findState(initialStateId);
            if (initialState == nullptr)
            {
                LOG_ERROR("[StateMachine] Configured start state does not exist.");
                return;
            }
            m_currentState = initialState->id;
            m_runtimeStarted = true;
            enterState(*initialState);
            return;
        }

        const StateMachineTransition* selectedTransition = nullptr;
        for (const StateMachineTransition& transition : asset->transitions)
        {
            if (transition.sourceState != m_currentState || !conditionsPass(transition))
                continue;
            if (selectedTransition == nullptr || transition.priority < selectedTransition->priority)
                selectedTransition = &transition;
        }
        if (selectedTransition == nullptr)
            return;

        const StateMachineState* const source = findState(selectedTransition->sourceState);
        const StateMachineState* const destination = findState(selectedTransition->destinationState);
        if (source == nullptr || destination == nullptr)
        {
            LOG_ERROR("[StateMachine] Transition references a missing state.");
            return;
        }
        if (!executeActions(source->onExit))
            return;
        if (!selectedTransition->eventName.empty())
        {
            const auto event = std::ranges::find(m_pendingEvents, selectedTransition->eventName);
            if (event != m_pendingEvents.end())
                m_pendingEvents.erase(event);
        }
        for (const StateMachineCondition& condition : selectedTransition->conditions)
            if (condition.mode == StateMachineConditionMode::Triggered)
                resetTrigger(condition.parameterId);
        m_currentState = destination->id;
        enterState(*destination);
    }

    void StateMachineComponent::enterState(const StateMachineState& state)
    {
        executeActions(state.onEnter);
    }

    bool StateMachineComponent::executeActions(const std::vector<StateMachineAction>& actions)
    {
        const std::shared_ptr<const StateMachineAsset> asset = m_asset;
        const StateMachineID stateId = m_currentState;
        for (const StateMachineAction& action : actions)
        {
            switch (action.type)
            {
            case StateMachineActionType::EmitEvent:
                emitEvent(action.eventName);
                break;
            case StateMachineActionType::PlayAnimationClip:
            {
                GameObject* const object = getGameObject();
                AnimatorComponent* const animator = object != nullptr ? object->getComponent<AnimatorComponent>() : nullptr;
                const AnimationClipHandle clip = AnimationAssetManager::instance().loadClip(action.animationClipPath);
                const auto clipAsset = AnimationAssetManager::instance().getClip(clip);
                if (animator == nullptr || !clipAsset || clipAsset->guid != action.animationClipGuid
                    || !animator->setAnimation(animator->getSkeleton(), clip) || !animator->play())
                {
                    LOG_ERROR("[StateMachine] Play Animation Clip action failed for state '{}'.", getCurrentStateName());
                }
                break;
            }
            case StateMachineActionType::Custom:
                if (!StateMachineActionRegistry::instance().execute(*this, action))
                    LOG_ERROR("[StateMachine] Custom action '{}' failed.", action.customType);
                break;
            }
            if (m_asset != asset || m_currentState != stateId || !m_runtimeStarted)
                return false;
        }
        return true;
    }

    void StateMachineComponent::onStart()
    {
        if (refreshAsset())
            updateStateMachine();
    }

    void StateMachineComponent::onUpdate(float)
    {
        if (refreshAsset())
            updateStateMachine();
    }

    void StateMachineComponent::onPayloadRestored()
    {
        m_currentState = 0;
        m_runtimeStarted = false;
        m_pendingEvents.clear();
        m_emittedEvents.clear();
    }

    std::uint32_t StateMachineComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_STATE_MACHINE_COMPONENT_VERSION;
    }

    bool StateMachineComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        ObjectGUID guid;
        std::string pathText;
        StateMachineID startingState = m_startingState;
        if (m_assetHandle.isValid())
        {
            StateMachineAssetManager& manager = StateMachineAssetManager::instance();
            const auto asset = manager.get(m_assetHandle);
            const auto path = manager.getPath(m_assetHandle);
            if (!asset || path.empty() || manager.isDirty(m_assetHandle)
                || (m_asset != nullptr && m_asset->guid != asset->guid))
            {
                LOG_ERROR("[StateMachine] Save the assigned asset before saving this GameObject.");
                return false;
            }
            guid = asset->guid;
            if (startingState != 0
                && std::ranges::find(asset->states, startingState, &StateMachineState::id) == asset->states.end())
            {
                LOG_WARNING("[StateMachine] Start-state override no longer exists; saving the asset default.");
                startingState = 0;
            }
            const auto text = String::fromWide(path.c_str());
            if (text.size() == 0)
            {
                LOG_ERROR("[StateMachine] Cannot encode the assigned asset path as UTF-8.");
                return false;
            }
            pathText.assign(text.data(), text.size());
        }

        flatbuffers::FlatBufferBuilder builder;
        const Serialization::AssetGuid flatGuid(guid.high, guid.low);
        const auto root = Serialization::CreateStateMachineComponentPayload(builder, &flatGuid,
            builder.CreateString(pathText), startingState);
        Serialization::FinishStateMachineComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool StateMachineComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        const Serialization::StateMachineComponentPayload* const source = readPayload(version, payload);
        if (source == nullptr || source->asset_guid() == nullptr || source->asset_path() == nullptr)
            return false;
        const ObjectGUID guid{ source->asset_guid()->high(), source->asset_guid()->low() };
        const std::string_view pathText = source->asset_path()->string_view();
        if (pathText.find('\0') != std::string_view::npos || guid.isValid() != !pathText.empty())
        {
            LOG_ERROR("[StateMachine] Asset GUID/path reference is invalid.");
            return false;
        }

        StateMachineAssetHandle handle;
        std::shared_ptr<const StateMachineAsset> asset;
        if (guid.isValid())
        {
            const std::wstring path = String(pathText).toWide();
            if (path.empty())
            {
                LOG_ERROR("[StateMachine] Asset path is not valid UTF-8.");
                return false;
            }
            handle = StateMachineAssetManager::instance().load(path, guid);
            if (!handle.isValid())
                return false;
            asset = StateMachineAssetManager::instance().get(handle);
            if (!asset)
                return false;
        }
        if (source->starting_state() != 0 && (asset == nullptr
            || std::ranges::find(asset->states, source->starting_state(), &StateMachineState::id) == asset->states.end()))
        {
            LOG_ERROR("[StateMachine] Saved starting state does not exist in the assigned asset.");
            return false;
        }
        if (!setStateMachine(handle))
            return false;
        if (source->starting_state() != 0 && !setStartingState(source->starting_state()))
            return false;
        return true;
    }

    bool StateMachineComponent::copyConfigurationTo(Component& destination) const
    {
        auto* const target = dynamic_cast<StateMachineComponent*>(&destination);
        if (target == nullptr)
        {
            LOG_ERROR("[StateMachine] Clone component type mismatch.");
            return false;
        }
        if (!target->setStateMachine(m_assetHandle))
            return false;
        if (!target->setStartingState(m_startingState))
        {
            LOG_WARNING("[StateMachine] Clone start-state override no longer exists; using the asset default.");
            target->setStartingState(0);
        }
        return true;
    }

    void StateMachineComponent::onImGui()
    {
        refreshAsset();
        if (m_asset != nullptr)
        {
            ImGui::Text("Asset: %s", m_asset->name.c_str());
            const StateMachineState* const defaultState = findState(m_asset->defaultState);
            const char* const defaultName = defaultState != nullptr ? defaultState->name.c_str() : "<invalid>";
            std::string startLabel = m_startingState == 0 ? std::string("Asset Default (") + defaultName + ")"
                : findState(m_startingState) != nullptr ? findState(m_startingState)->name : "Invalid";
            const std::string_view currentStateName = getCurrentStateName();
            if (currentStateName.empty())
                ImGui::TextDisabled("Current State: <not started>");
            else
                ImGui::Text("Current State: %.*s", static_cast<int>(currentStateName.size()), currentStateName.data());
            if (ImGui::BeginCombo("Starting State", startLabel.c_str()))
            {
                const std::string defaultOption = std::string("Asset Default (") + defaultName + ")";
                if (ImGui::Selectable(defaultOption.c_str(), m_startingState == 0))
                    setStartingState(0);
                for (const StateMachineState& state : m_asset->states)
                    if (ImGui::Selectable(state.name.c_str(), m_startingState == state.id))
                        setStartingState(state.id);
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Edit the graph in Window > State Machine.");
        }
        else
            ImGui::TextDisabled("No State Machine asset assigned.");

        if (ImGui::Button("Browse State Machine..."))
        {
            const GameObject* const object = getGameObject();
            if (object == nullptr)
                return;
            const ObjectGUID objectGuid = object->getGUID();
            const HWND owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([objectGuid, owner]
            {
                static constexpr std::array filters = { FileDialogFilter{ L"State Machine", L"*.statemachine" } };
                std::vector<std::filesystem::path> paths;
                const DialogResult result = Dialog::openFile(paths, L"Open State Machine",
                    L"Assets", filters, false, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok || paths.size() != 1)
                {
                    LOG_ERROR("[StateMachine] State Machine asset selection dialog failed.");
                    return;
                }
                const StateMachineAssetHandle handle = StateMachineAssetManager::instance().load(paths.front());
                if (!handle.isValid())
                    return;

                SceneManager& scenes = SceneManager::instance();
                GameObject* target = scenes.getActiveScene() != nullptr
                    ? scenes.getActiveScene()->find(objectGuid) : nullptr;
                if (target == nullptr)
                    target = scenes.getPersistentScene()->find(objectGuid);
                auto* component = target != nullptr ? target->getComponent<StateMachineComponent>() : nullptr;
                if (component == nullptr || !component->setStateMachine(handle))
                    LOG_ERROR("[StateMachine] Could not assign the selected State Machine asset.");
            });
        }
        if (m_assetHandle.isValid())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                setStateMachine({});
        }
    }
}
