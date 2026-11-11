#pragma once

#include "Assets\StateMachine\StateMachineAsset.h"
#include "Core\GameObject\Behaviour.h"
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Engine
{
    /**
     * @brief Executes one shared StateMachineAsset with per-GameObject runtime values.
     * @thread_safety Main thread only. Custom actions run synchronously during lifecycle updates.
     */
    class StateMachineComponent final : public Behaviour
    {
    public:
        StateMachineComponent() noexcept;
        ~StateMachineComponent() override = default;
        GE_DISABLE_COPY_AND_MOVE(StateMachineComponent);

        /** @brief Assigns a validated asset handle, or clears the assignment with an invalid handle. */
        bool setStateMachine(StateMachineAssetHandle handle);
        /** @brief Returns the assigned asset handle. */
        StateMachineAssetHandle getStateMachine() const noexcept { return m_assetHandle; }
        /** @brief Returns the immutable authored graph snapshot, or null when unassigned. */
        std::shared_ptr<const StateMachineAsset> getAssetSnapshot() const noexcept { return m_asset; }

        /** @brief Sets the per-instance start state; zero selects the asset default. */
        bool setStartingState(StateMachineID stateId);
        /** @brief Gets the per-instance start-state override, or zero for the asset default. */
        StateMachineID getStartingState() const noexcept { return m_startingState; }
        /** @brief Gets the current runtime state ID; zero means not started. */
        StateMachineID getCurrentState() const noexcept { return m_currentState; }
        /** @brief Gets the current state name, or an empty view if not started. */
        std::string_view getCurrentStateName() const noexcept;

        /** @brief Sets a finite Float parameter on this instance. */
        bool setFloat(StateMachineParameterID id, float value) noexcept;
        /** @brief Sets an Int parameter on this instance. */
        bool setInt(StateMachineParameterID id, std::int32_t value) noexcept;
        /** @brief Sets a Bool parameter on this instance. */
        bool setBool(StateMachineParameterID id, bool value) noexcept;
        /** @brief Raises a Trigger parameter on this instance. */
        bool setTrigger(StateMachineParameterID id) noexcept;
        /** @brief Clears a Trigger parameter on this instance. */
        bool resetTrigger(StateMachineParameterID id) noexcept;
        /** @brief Reads a Float parameter; returns false for a missing ID or type mismatch. */
        bool getFloat(StateMachineParameterID id, float& value) const noexcept;
        /** @brief Reads an Int parameter; returns false for a missing ID or type mismatch. */
        bool getInt(StateMachineParameterID id, std::int32_t& value) const noexcept;
        /** @brief Reads a Bool parameter; returns false for a missing ID or type mismatch. */
        bool getBool(StateMachineParameterID id, bool& value) const noexcept;

        /** @brief Queues a named input event for transition evaluation. */
        bool sendEvent(std::string_view eventName);
        /** @brief Consumes one matching output event emitted by an Enter/Exit action. */
        bool consumeEvent(std::string_view eventName);
        /** @brief Clears parameters, events, and runtime state; the configured start state enters on next update. */
        void reset() noexcept;

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        bool copyConfigurationTo(Component& destination) const override;

    protected:
        void onStart() override;
        void onUpdate(float deltaTime) override;
        void onPayloadRestored() override;
        void onImGui() override;

    private:
        struct RuntimeParameter
        {
            StateMachineParameterID id = 0;
            StateMachineParameterType type = StateMachineParameterType::Float;
            float floatValue = 0.0f;
            std::int32_t intValue = 0;
            bool boolValue = false;
        };

        RuntimeParameter* findParameter(StateMachineParameterID id) noexcept;
        const RuntimeParameter* findParameter(StateMachineParameterID id) const noexcept;
        const StateMachineState* findState(StateMachineID id) const noexcept;
        bool refreshAsset();
        bool conditionsPass(const StateMachineTransition& transition) const noexcept;
        void updateStateMachine();
        void enterState(const StateMachineState& state);
        bool executeActions(const std::vector<StateMachineAction>& actions);
        bool emitEvent(std::string_view eventName);

        StateMachineAssetHandle m_assetHandle;
        std::shared_ptr<const StateMachineAsset> m_asset;
        std::vector<RuntimeParameter> m_parameters;
        std::vector<std::string> m_pendingEvents;
        std::vector<std::string> m_emittedEvents;
        StateMachineID m_startingState = 0;
        StateMachineID m_currentState = 0;
        bool m_runtimeStarted = false;
        bool m_missingAssetReported = false;
    };
}
