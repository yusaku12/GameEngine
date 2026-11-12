#include "Pch.h"
#include "Components\Rendering\SkyAtmosphereComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Generated\FlatBuffers\SkyAtmosphereComponent_generated.h"
#include <imgui.h>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    namespace
    {
        const auto skyAtmosphereType =
            ComponentRegistry::instance().registerType<SkyAtmosphereComponent>("Sky Atmosphere", false, false, false);

        bool isFiniteNonNegative(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
                && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f
                && value.x <= 10.0f && value.y <= 10.0f && value.z <= 10.0f;
        }
    }

    bool SkyAtmosphereComponent::setSettings(const SkyAtmosphereSettings& value) noexcept
    {
        if (!std::isfinite(value.groundRadiusKm) || value.groundRadiusKm < 1.0f || value.groundRadiusKm > 1000000.0f
            || !std::isfinite(value.atmosphereHeightKm) || value.atmosphereHeightKm < 0.1f
            || value.atmosphereHeightKm > 10000.0f
            || !std::isfinite(value.worldUnitsPerKilometer) || value.worldUnitsPerKilometer < 0.001f
            || value.worldUnitsPerKilometer > 1000000000.0f
            || !isFiniteNonNegative(value.rayleighScattering)
            || !std::isfinite(value.rayleighScaleHeightKm) || value.rayleighScaleHeightKm < 0.01f
            || value.rayleighScaleHeightKm > 1000.0f
            || !std::isfinite(value.mieScattering) || value.mieScattering < 0.0f || value.mieScattering > 10.0f
            || !std::isfinite(value.mieAbsorption) || value.mieAbsorption < 0.0f || value.mieAbsorption > 10.0f
            || !std::isfinite(value.mieScaleHeightKm) || value.mieScaleHeightKm < 0.01f
            || value.mieScaleHeightKm > 1000.0f
            || !std::isfinite(value.mieAnisotropy) || value.mieAnisotropy < -0.95f || value.mieAnisotropy > 0.95f
            || !isFiniteNonNegative(value.ozoneAbsorption)
            || !std::isfinite(value.absorptionScale) || value.absorptionScale < 0.0f || value.absorptionScale > 100.0f
            || !std::isfinite(value.aerialPerspectiveViewDistanceScale)
            || value.aerialPerspectiveViewDistanceScale < 0.0f || value.aerialPerspectiveViewDistanceScale > 100.0f
            || !std::isfinite(value.sunDiskIntensity) || value.sunDiskIntensity < 0.0f
            || value.sunDiskIntensity > 100000.0f
            || !std::isfinite(value.sunAngularRadiusDegrees) || value.sunAngularRadiusDegrees < 0.01f
            || value.sunAngularRadiusDegrees > 10.0f
            || !std::isfinite(value.sunLightIntensityScale) || value.sunLightIntensityScale < 0.0f
            || value.sunLightIntensityScale > 100000.0f)
        {
            LOG_ERROR("[Sky Atmosphere] Invalid atmosphere configuration.");
            return false;
        }
        m_settings = value;
        return true;
    }

    bool SkyAtmosphereComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const Serialization::Vec3 rayleigh(m_settings.rayleighScattering.x, m_settings.rayleighScattering.y,
            m_settings.rayleighScattering.z);
        const Serialization::Vec3 ozone(m_settings.ozoneAbsorption.x, m_settings.ozoneAbsorption.y,
            m_settings.ozoneAbsorption.z);
        const auto root = Serialization::CreateSkyAtmosphereComponentPayload(builder, m_settings.enabled,
            m_settings.groundRadiusKm, m_settings.atmosphereHeightKm, m_settings.worldUnitsPerKilometer,
            &rayleigh, m_settings.rayleighScaleHeightKm, m_settings.mieScattering, m_settings.mieAbsorption,
            m_settings.mieScaleHeightKm, m_settings.mieAnisotropy, &ozone, m_settings.absorptionScale,
            m_settings.aerialPerspectiveViewDistanceScale, m_settings.sunDiskIntensity,
            m_settings.sunAngularRadiusDegrees, m_settings.sunLightIntensityScale);
        Serialization::FinishSkyAtmosphereComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool SkyAtmosphereComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != 1 || payload.size() < 8
            || !Serialization::VerifySkyAtmosphereComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Sky Atmosphere] Invalid atmosphere payload.");
            return false;
        }
        const auto* const source = Serialization::GetSkyAtmosphereComponentPayload(payload.data());
        if (!source->rayleigh_scattering() || !source->ozone_absorption())
        {
            LOG_ERROR("[Sky Atmosphere] Atmosphere payload is missing scattering data.");
            return false;
        }
        return setSettings({ source->enabled(), source->ground_radius_km(), source->atmosphere_height_km(),
            source->world_units_per_kilometer(),
            Vector3(source->rayleigh_scattering()->x(), source->rayleigh_scattering()->y(),
                source->rayleigh_scattering()->z()),
            source->rayleigh_scale_height_km(), source->mie_scattering(), source->mie_absorption(),
            source->mie_scale_height_km(), source->mie_anisotropy(),
            Vector3(source->ozone_absorption()->x(), source->ozone_absorption()->y(),
                source->ozone_absorption()->z()),
            source->absorption_scale(), source->aerial_perspective_view_distance_scale(),
            source->sun_disk_intensity(), source->sun_angular_radius_degrees(),
            source->sun_light_intensity_scale() });
    }

    void SkyAtmosphereComponent::onImGui()
    {
        SkyAtmosphereSettings value = m_settings;
        bool changed = ImGui::Checkbox("Enabled", &value.enabled);
        changed |= ImGui::DragFloat("Ground Radius (km)", &value.groundRadiusKm, 1.0f, 1.0f, 1000000.0f);
        changed |= ImGui::DragFloat("Atmosphere Height (km)", &value.atmosphereHeightKm, 0.1f, 0.1f, 10000.0f);
        changed |= ImGui::DragFloat("World Units per km", &value.worldUnitsPerKilometer, 1.0f, 0.001f, 1000000000.0f);
        changed |= ImGui::ColorEdit3("Rayleigh Scattering", &value.rayleighScattering.x);
        changed |= ImGui::DragFloat("Rayleigh Scale Height (km)", &value.rayleighScaleHeightKm, 0.1f, 0.01f, 1000.0f);
        changed |= ImGui::DragFloat("Mie Scattering", &value.mieScattering, 0.0001f, 0.0f, 10.0f, "%.5f");
        changed |= ImGui::DragFloat("Mie Absorption", &value.mieAbsorption, 0.0001f, 0.0f, 10.0f, "%.5f");
        changed |= ImGui::DragFloat("Mie Scale Height (km)", &value.mieScaleHeightKm, 0.1f, 0.01f, 1000.0f);
        changed |= ImGui::SliderFloat("Mie Anisotropy", &value.mieAnisotropy, -0.95f, 0.95f);
        changed |= ImGui::ColorEdit3("Ozone Absorption", &value.ozoneAbsorption.x);
        changed |= ImGui::DragFloat("Absorption Scale", &value.absorptionScale, 0.01f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("Aerial Perspective Distance Scale",
            &value.aerialPerspectiveViewDistanceScale, 0.01f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("Sun Disk Intensity", &value.sunDiskIntensity, 0.1f, 0.0f, 100000.0f);
        changed |= ImGui::DragFloat("Sun Angular Radius (degrees)",
            &value.sunAngularRadiusDegrees, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("Sun Light Intensity Scale",
            &value.sunLightIntensityScale, 0.1f, 0.0f, 100000.0f);
        if (changed)
            setSettings(value);
    }
}
