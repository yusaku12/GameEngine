#include "Pch.h"
#include "Components\Rendering\VolumetricCloudComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Generated\FlatBuffers\VolumetricCloudComponent_generated.h"
#include <imgui.h>
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        const auto cloudType =
            ComponentRegistry::instance().registerType<VolumetricCloudComponent>("Volumetric Cloud", false, false, false);

        bool inRange(const float value, const float minimum, const float maximum) noexcept
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        }
    }

    bool VolumetricCloudComponent::setSettings(const VolumetricCloudSettings& value) noexcept
    {
        if (!inRange(value.baseHeightKm, -100.0f, 100.0f)
            || !inRange(value.thicknessKm, 0.1f, 20.0f)
            || !inRange(value.worldUnitsPerKilometer, 0.001f, 1000000000.0f)
            || !inRange(value.coverage, 0.0f, 1.0f)
            || !inRange(value.density, 0.0f, 10.0f)
            || !inRange(value.noiseScaleKm, 0.1f, 100.0f)
            || !inRange(value.maxDistanceKm, 1.0f, 500.0f)
            || value.sampleCount < 8 || value.sampleCount > 64)
        {
            LOG_ERROR("[Volumetric Cloud] Invalid cloud configuration.");
            return false;
        }
        m_settings = value;
        return true;
    }

    bool VolumetricCloudComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto root = Serialization::CreateVolumetricCloudComponentPayload(builder, m_settings.enabled,
            m_settings.baseHeightKm, m_settings.thicknessKm, m_settings.worldUnitsPerKilometer,
            m_settings.coverage, m_settings.density, m_settings.noiseScaleKm,
            m_settings.maxDistanceKm, m_settings.sampleCount);
        Serialization::FinishVolumetricCloudComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool VolumetricCloudComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != 1 || payload.size() < 8
            || !Serialization::VerifyVolumetricCloudComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Volumetric Cloud] Invalid cloud payload.");
            return false;
        }
        const auto* const source = Serialization::GetVolumetricCloudComponentPayload(payload.data());
        return setSettings({ source->enabled(), source->base_height_km(), source->thickness_km(),
            source->world_units_per_kilometer(), source->coverage(), source->density(),
            source->noise_scale_km(), source->max_distance_km(), source->sample_count() });
    }

    void VolumetricCloudComponent::onImGui()
    {
        VolumetricCloudSettings value = m_settings;
        bool changed = ImGui::Checkbox("Enabled", &value.enabled);
        changed |= ImGui::DragFloat("Base Height (km)", &value.baseHeightKm, 0.01f, -100.0f, 100.0f);
        changed |= ImGui::DragFloat("Thickness (km)", &value.thicknessKm, 0.01f, 0.1f, 20.0f);
        changed |= ImGui::DragFloat("World Units per km", &value.worldUnitsPerKilometer, 1.0f, 0.001f, 1000000000.0f);
        changed |= ImGui::SliderFloat("Coverage", &value.coverage, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Density", &value.density, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Noise Scale (km)", &value.noiseScaleKm, 0.01f, 0.1f, 100.0f);
        changed |= ImGui::DragFloat("Maximum Distance (km)", &value.maxDistanceKm, 0.1f, 1.0f, 500.0f);
        int samples = static_cast<int>(value.sampleCount);
        if (ImGui::SliderInt("View Samples", &samples, 8, 64))
        {
            value.sampleCount = static_cast<std::uint32_t>(samples);
            changed = true;
        }
        if (changed)
            setSettings(value);
    }
}
