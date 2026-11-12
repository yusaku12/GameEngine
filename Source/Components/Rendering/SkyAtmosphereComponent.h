#pragma once

#include <span>

#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Physical atmosphere parameters; distances are expressed in kilometers. */
    struct SkyAtmosphereSettings
    {
        bool enabled = true; //!< Enables atmosphere rendering.
        float groundRadiusKm = 6360.0f; //!< Planet radius in kilometers.
        float atmosphereHeightKm = 100.0f; //!< Shell height above the ground radius.
        float worldUnitsPerKilometer = 1000.0f; //!< Conversion from engine units to kilometers.
        Vector3 rayleighScattering = Vector3(0.005802f, 0.013558f, 0.0331f); //!< Wavelength-dependent scattering.
        float rayleighScaleHeightKm = 8.0f; //!< Rayleigh density e-folding height.
        float mieScattering = 0.003996f; //!< Aerosol scattering coefficient.
        float mieAbsorption = 0.0044f; //!< Aerosol absorption coefficient.
        float mieScaleHeightKm = 1.2f; //!< Aerosol density e-folding height.
        float mieAnisotropy = 0.8f; //!< Henyey-Greenstein phase anisotropy.
        Vector3 ozoneAbsorption = Vector3(0.00065f, 0.001881f, 0.000085f); //!< RGB ozone absorption coefficients.
        float absorptionScale = 1.0f; //!< Global ozone absorption multiplier.
        float aerialPerspectiveViewDistanceScale = 1.0f; //!< Artistic scale applied to surface path length.
        float sunDiskIntensity = 20.0f; //!< Direct sun-disk radiance multiplier.
        float sunAngularRadiusDegrees = 0.27f; //!< Apparent angular radius of each atmosphere sun.
        float sunLightIntensityScale = 20.0f; //!< Radiance multiplier for directional atmosphere lights.
    };

    /**
     * @brief Scene-wide physically based sky, atmospheric scattering, and aerial perspective.
     * @details The GameObject position is the planet center. Use a Directional Light configured
     *          as an atmosphere sun to illuminate the atmosphere.
     * @thread_safety Main thread only. Render data is copied into LightingFrame snapshots.
     */
    class SkyAtmosphereComponent final : public Component
    {
    public:
        /** @brief Returns the authored atmosphere configuration. */
        const SkyAtmosphereSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces all atmosphere settings; invalid values are rejected. */
        bool setSettings(const SkyAtmosphereSettings& settings) noexcept;
        /** @brief Returns the persisted configuration version. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Serializes atmosphere settings, excluding runtime state. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores a versioned atmosphere payload. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:
        void onImGui() override;

    private:
        SkyAtmosphereSettings m_settings;
    };
}
