#pragma once

#include <span>

#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Global exponential-height fog settings. Distances use engine world units. */
    struct VolumetricFogSettings
    {
        bool enabled = true; //!< Enables fog rendering.
        float density = 0.02f; //!< Base extinction density at the fog height.
        float heightFalloff = 0.2f; //!< Exponential density falloff per world unit above the fog height.
        float extinctionScale = 1.0f; //!< Scales absorption and in-scattering together.
        Vector3 albedo = Vector3::One; //!< Scattering color in linear space.
        Vector3 emissive = Vector3::Zero; //!< Fog's emitted radiance in linear HDR.
        float scatteringDistribution = 0.0f; //!< Henyey-Greenstein anisotropy.
        float maxOpacity = 0.98f; //!< Maximum accumulated fog opacity.
        float startDistance = 0.0f; //!< Camera distance before fog starts.
        float nearFadeInDistance = 0.0f; //!< Distance over which fog fades in after its start.
        float viewDistance = 10000.0f; //!< Maximum camera distance evaluated by the fog integrator.
        std::uint32_t sampleCount = 16; //!< Number of view-ray integration samples.
    };

    /**
     * @brief Scene-wide exponential-height volumetric fog for Deferred HDR rendering.
     * @details The GameObject world Y position is the fog height.
     * @thread_safety Main thread only. Rendering uses a LightingFrame snapshot.
     */
    class VolumetricFogComponent final : public Component
    {
    public:
        /** @brief Returns the authored fog configuration. */
        const VolumetricFogSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces all fog settings. */
        bool setSettings(const VolumetricFogSettings& settings) noexcept;
        /** @brief Returns the persisted configuration version. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Serializes fog settings, excluding runtime state. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores a versioned fog payload. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:
        void onImGui() override;

    private:
        VolumetricFogSettings m_settings;
    };
}
