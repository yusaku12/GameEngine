#pragma once

#include <span>
#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Supported direct-light shapes. */
    enum class LightKind : std::uint8_t { Directional, Point, Spot };

    /** @brief Editable direct-light and directional shadow configuration. */
    struct LightSettings
    {
        LightKind kind = LightKind::Directional;
        Vector3 color = Vector3::One;
        float intensity = 1.0f;
        float range = 10.0f;
        float innerAngle = 25.0f;
        float outerAngle = 40.0f;
        bool castsShadow = false;
        float shadowDistance = 50.0f;
        float shadowBias = 0.001f;
        float normalBias = 0.02f;
    };

    /**
     * @brief A scene light; local +Z is its emission direction.
     * @thread_safety Not thread-safe. Access follows the engine frame barrier.
     */
    class LightComponent final : public Component
    {
    public:
        /** @brief Returns configuration without exposing mutable state. */
        const LightSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces configuration; invalid values are logged and rejected. */
        bool setSettings(const LightSettings& settings) noexcept;
        /** @brief Returns the persisted configuration version. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Serializes light settings, excluding runtime state. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores a versioned light payload. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        LightSettings m_settings;
    };
}
