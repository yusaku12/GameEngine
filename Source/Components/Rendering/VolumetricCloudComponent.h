#pragma once

#include <span>

#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Ground-view cloud layer parameters; distances are in kilometers. */
    struct VolumetricCloudSettings
    {
        bool enabled = true; //!< Enables cloud rendering.
        float baseHeightKm = 2.0f; //!< Height above the component's world-space Y position.
        float thicknessKm = 2.0f; //!< Thickness of the horizontal cloud layer.
        float worldUnitsPerKilometer = 1000.0f; //!< Conversion from engine units to kilometers.
        float coverage = 0.55f; //!< Fractional cloud coverage.
        float density = 1.0f; //!< Extinction coefficient multiplier.
        float noiseScaleKm = 4.0f; //!< Size of the largest procedural noise features.
        float maxDistanceKm = 80.0f; //!< Maximum camera ray distance.
        std::uint32_t sampleCount = 32; //!< Number of view-ray samples.
    };

    /**
     * @brief Scene-wide, procedural volumetric cloud layer for ground views.
     * @details The GameObject position anchors the layer and its noise. Directional
     *          atmosphere suns illuminate it; rendering requires Deferred and Skybox.
     * @thread_safety Main thread only. Rendering uses a LightingFrame snapshot.
     */
    class VolumetricCloudComponent final : public Component
    {
    public:
        /** @brief Returns the authored cloud configuration. */
        const VolumetricCloudSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces the cloud configuration. */
        bool setSettings(const VolumetricCloudSettings& settings) noexcept;
        /** @brief Returns the persisted configuration version. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Serializes authored settings. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores a versioned payload. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:
        void onImGui() override;

    private:
        VolumetricCloudSettings m_settings;
    };
}
