#include "Pch.h"
#include "Components\Rendering\VolumetricFogComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Generated\FlatBuffers\VolumetricFogComponent_generated.h"
#include <imgui.h>
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        const auto volumetricFogType =
            ComponentRegistry::instance().registerType<VolumetricFogComponent>("Volumetric Fog", false, false, false);

        bool inRange(const float value, const float minimum, const float maximum) noexcept
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        }

        bool inRange(const Vector3& value, const float minimum, const float maximum) noexcept
        {
            return inRange(value.x, minimum, maximum)
                && inRange(value.y, minimum, maximum)
                && inRange(value.z, minimum, maximum);
        }
    }

    bool VolumetricFogComponent::setSettings(const VolumetricFogSettings& value) noexcept
    {
        if (!inRange(value.density, 0.0f, 1000.0f)
            || !inRange(value.heightFalloff, 0.0f, 10.0f)
            || !inRange(value.extinctionScale, 0.0f, 1000.0f)
            || !inRange(value.albedo, 0.0f, 1.0f)
            || !inRange(value.emissive, 0.0f, 100000.0f)
            || !inRange(value.scatteringDistribution, -0.95f, 0.95f)
            || !inRange(value.maxOpacity, 0.0f, 1.0f)
            || !inRange(value.startDistance, 0.0f, 10000000.0f)
            || !inRange(value.nearFadeInDistance, 0.0f, 10000000.0f)
            || !inRange(value.viewDistance, 1.0f, 10000000.0f)
            || value.sampleCount < 4 || value.sampleCount > 64)
        {
            LOG_ERROR("[Volumetric Fog] Invalid fog configuration.");
            return false;
        }
        m_settings = value;
        return true;
    }

    bool VolumetricFogComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const Serialization::Vec3 albedo(m_settings.albedo.x, m_settings.albedo.y, m_settings.albedo.z);
        const Serialization::Vec3 emissive(m_settings.emissive.x, m_settings.emissive.y, m_settings.emissive.z);
        const auto root = Serialization::CreateVolumetricFogComponentPayload(builder, m_settings.enabled,
            m_settings.density, m_settings.heightFalloff, m_settings.extinctionScale, &albedo, &emissive,
            m_settings.scatteringDistribution, m_settings.maxOpacity, m_settings.startDistance,
            m_settings.nearFadeInDistance, m_settings.viewDistance, m_settings.sampleCount);
        Serialization::FinishVolumetricFogComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool VolumetricFogComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != 1 || payload.size() < 8
            || !Serialization::VerifyVolumetricFogComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Volumetric Fog] Invalid fog payload.");
            return false;
        }
        const auto* const source = Serialization::GetVolumetricFogComponentPayload(payload.data());
        if (!source->albedo() || !source->emissive())
        {
            LOG_ERROR("[Volumetric Fog] Fog payload is missing color data.");
            return false;
        }
        return setSettings({ source->enabled(), source->density(), source->height_falloff(),
            source->extinction_scale(),
            Vector3(source->albedo()->x(), source->albedo()->y(), source->albedo()->z()),
            Vector3(source->emissive()->x(), source->emissive()->y(), source->emissive()->z()),
            source->scattering_distribution(), source->max_opacity(), source->start_distance(),
            source->near_fade_in_distance(), source->view_distance(), source->sample_count() });
    }

    void VolumetricFogComponent::onImGui()
    {
        VolumetricFogSettings value = m_settings;
        bool changed = ImGui::Checkbox("Enabled", &value.enabled);
        changed |= ImGui::DragFloat("Density", &value.density, 0.001f, 0.0f, 10.0f, "%.4f");
        changed |= ImGui::DragFloat("Height Falloff", &value.heightFalloff, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Extinction Scale", &value.extinctionScale, 0.01f, 0.0f, 1000.0f);
        changed |= ImGui::ColorEdit3("Albedo", &value.albedo.x);
        changed |= ImGui::ColorEdit3("Emissive", &value.emissive.x, ImGuiColorEditFlags_HDR);
        changed |= ImGui::SliderFloat("Scattering Distribution", &value.scatteringDistribution, -0.95f, 0.95f);
        changed |= ImGui::SliderFloat("Maximum Opacity", &value.maxOpacity, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Start Distance", &value.startDistance, 1.0f, 0.0f, 10000000.0f);
        changed |= ImGui::DragFloat("Near Fade In Distance", &value.nearFadeInDistance, 1.0f, 0.0f, 10000000.0f);
        changed |= ImGui::DragFloat("View Distance", &value.viewDistance, 10.0f, 1.0f, 10000000.0f);
        int samples = static_cast<int>(value.sampleCount);
        if (ImGui::SliderInt("View Samples", &samples, 4, 64))
        {
            value.sampleCount = static_cast<std::uint32_t>(samples);
            changed = true;
        }
        if (changed)
            setSettings(value);
    }
}
