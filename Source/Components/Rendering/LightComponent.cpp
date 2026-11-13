#include "Pch.h"
#include "Components\Rendering\LightComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Generated\FlatBuffers\LightComponent_generated.h"
#include <imgui.h>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    namespace
    {
        const auto lightType = ComponentRegistry::instance().registerType<LightComponent>("Light", false, false, false);
    }

    bool LightComponent::setSettings(const LightSettings& value) noexcept
    {
        if (value.kind > LightKind::Spot || !std::isfinite(value.color.x) || !std::isfinite(value.color.y)
            || !std::isfinite(value.color.z) || value.color.x < 0 || value.color.y < 0 || value.color.z < 0
            || value.color.x > 1 || value.color.y > 1 || value.color.z > 1
            || !std::isfinite(value.intensity) || value.intensity < 0 || value.intensity > 100000
            || !std::isfinite(value.range) || value.range <= 0 || value.range > 100000
            || !std::isfinite(value.innerAngle) || !std::isfinite(value.outerAngle)
            || value.innerAngle < 0 || value.outerAngle <= value.innerAngle || value.outerAngle >= 179
            || !std::isfinite(value.shadowDistance) || value.shadowDistance < 1 || value.shadowDistance > 10000
            || !std::isfinite(value.shadowBias) || value.shadowBias < 0 || value.shadowBias > 0.1f
            || !std::isfinite(value.normalBias) || value.normalBias < 0 || value.normalBias > 10
            || (value.atmosphereSunLight && value.kind != LightKind::Directional)
            || value.atmosphereSunIndex > 1
            || !std::isfinite(value.volumetricScatteringIntensity)
            || value.volumetricScatteringIntensity < 0 || value.volumetricScatteringIntensity > 1000
            || !std::isfinite(value.lightShaftBloomScale)
            || value.lightShaftBloomScale < 0 || value.lightShaftBloomScale > 10
            || !std::isfinite(value.lightShaftBloomThreshold)
            || value.lightShaftBloomThreshold < 0 || value.lightShaftBloomThreshold > 64
            || !std::isfinite(value.lightShaftBloomTint.x) || !std::isfinite(value.lightShaftBloomTint.y)
            || !std::isfinite(value.lightShaftBloomTint.z)
            || value.lightShaftBloomTint.x < 0 || value.lightShaftBloomTint.x > 1
            || value.lightShaftBloomTint.y < 0 || value.lightShaftBloomTint.y > 1
            || value.lightShaftBloomTint.z < 0 || value.lightShaftBloomTint.z > 1
            || !std::isfinite(value.lightShaftOcclusionMaskDarkness)
            || value.lightShaftOcclusionMaskDarkness < 0 || value.lightShaftOcclusionMaskDarkness > 1
            || ((value.lightShaftBloomEnabled || value.lightShaftOcclusionEnabled)
                && value.kind != LightKind::Directional))
        {
            LOG_ERROR("[Light] Invalid light or shadow configuration.");
            return false;
        }
        m_settings = value;
        return true;
    }

    bool LightComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const Serialization::Vec3 color(m_settings.color.x, m_settings.color.y, m_settings.color.z);
        const Serialization::Vec3 lightShaftBloomTint(
            m_settings.lightShaftBloomTint.x, m_settings.lightShaftBloomTint.y, m_settings.lightShaftBloomTint.z);
        const auto root = Serialization::CreateLightComponentPayload(builder, static_cast<std::uint8_t>(m_settings.kind),
            &color, m_settings.intensity, m_settings.range, m_settings.innerAngle, m_settings.outerAngle,
            m_settings.castsShadow, m_settings.shadowDistance, m_settings.shadowBias, m_settings.normalBias,
            m_settings.atmosphereSunLight, m_settings.atmosphereSunIndex,
            m_settings.volumetricScatteringIntensity, m_settings.lightShaftBloomEnabled,
            m_settings.lightShaftBloomScale, m_settings.lightShaftBloomThreshold, &lightShaftBloomTint,
            m_settings.lightShaftOcclusionEnabled, m_settings.lightShaftOcclusionMaskDarkness);
        Serialization::FinishLightComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool LightComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if ((version < 1 || version > 4) || payload.size() < 8
            || !Serialization::VerifyLightComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Light] Invalid light payload.");
            return false;
        }
        const auto* source = Serialization::GetLightComponentPayload(payload.data());
        if (!source->color())
        {
            LOG_ERROR("[Light] Missing light color.");
            return false;
        }
        if (version >= 4 && !source->light_shaft_bloom_tint())
        {
            LOG_ERROR("[Light] Missing light shaft bloom tint.");
            return false;
        }
        return setSettings({ static_cast<LightKind>(source->kind()),
            Vector3(source->color()->x(), source->color()->y(), source->color()->z()), source->intensity(),
            source->range(), source->inner_angle(), source->outer_angle(), source->casts_shadow(),
            source->shadow_distance(), source->shadow_bias(), source->normal_bias(),
            version >= 2 && source->atmosphere_sun_light(),
            static_cast<std::uint8_t>(version >= 2 ? source->atmosphere_sun_index() : 0),
            version >= 3 ? source->volumetric_scattering_intensity() : 1.0f,
            version >= 4 && source->light_shaft_bloom_enabled(),
            version >= 4 ? source->light_shaft_bloom_scale() : 1.0f,
            version >= 4 ? source->light_shaft_bloom_threshold() : 1.0f,
            version >= 4
                ? Vector3(source->light_shaft_bloom_tint()->x(), source->light_shaft_bloom_tint()->y(),
                    source->light_shaft_bloom_tint()->z())
                : Vector3::One,
            version >= 4 && source->light_shaft_occlusion_enabled(),
            version >= 4 ? source->light_shaft_occlusion_mask_darkness() : 0.5f });
    }

    void LightComponent::onImGui()
    {
        LightSettings value = m_settings;
        int kind = static_cast<int>(value.kind);
        bool changed = ImGui::Combo("Kind", &kind, "Directional\0Point\0Spot\0");
        value.kind = static_cast<LightKind>(kind);
        changed |= ImGui::ColorEdit3("Color", &value.color.x);
        changed |= ImGui::DragFloat("Intensity", &value.intensity, 0.05f, 0, 100000);
        changed |= ImGui::DragFloat("Volumetric Scattering Intensity",
            &value.volumetricScatteringIntensity, 0.05f, 0.0f, 10.0f);
        if (value.kind != LightKind::Directional)
            changed |= ImGui::DragFloat("Range", &value.range, 0.1f, 0.01f, 100000);
        if (value.kind == LightKind::Spot)
        {
            changed |= ImGui::SliderFloat("Inner Angle", &value.innerAngle, 0, value.outerAngle - 0.1f);
            changed |= ImGui::SliderFloat("Outer Angle", &value.outerAngle, value.innerAngle + 0.1f, 178.0f);
        }
        if (value.kind == LightKind::Directional)
        {
            changed |= ImGui::Checkbox("Cast Shadow", &value.castsShadow);
            changed |= ImGui::DragFloat("Shadow Distance", &value.shadowDistance, 0.5f, 1, 10000);
            changed |= ImGui::DragFloat("Shadow Bias", &value.shadowBias, 0.0001f, 0, 0.1f, "%.5f");
            changed |= ImGui::DragFloat("Normal Bias", &value.normalBias, 0.001f, 0, 10, "%.3f");
            changed |= ImGui::Checkbox("Atmosphere Sun Light", &value.atmosphereSunLight);
            if (value.atmosphereSunLight)
            {
                int sunIndex = static_cast<int>(value.atmosphereSunIndex);
                if (ImGui::SliderInt("Atmosphere Sun Index", &sunIndex, 0, 1))
                {
                    value.atmosphereSunIndex = static_cast<std::uint8_t>(sunIndex);
                    changed = true;
                }
            }
            changed |= ImGui::Checkbox("Light Shafts Bloom", &value.lightShaftBloomEnabled);
            if (value.lightShaftBloomEnabled)
            {
                changed |= ImGui::DragFloat("Light Shafts Bloom Scale",
                    &value.lightShaftBloomScale, 0.05f, 0.0f, 10.0f);
                changed |= ImGui::DragFloat("Light Shafts Bloom Threshold",
                    &value.lightShaftBloomThreshold, 0.05f, 0.0f, 64.0f);
                changed |= ImGui::ColorEdit3("Light Shafts Bloom Tint", &value.lightShaftBloomTint.x);
            }
            changed |= ImGui::Checkbox("Light Shafts Occlusion", &value.lightShaftOcclusionEnabled);
            if (value.lightShaftOcclusionEnabled)
                changed |= ImGui::SliderFloat("Light Shafts Occlusion Darkness",
                    &value.lightShaftOcclusionMaskDarkness, 0.0f, 1.0f);
        }
        if (changed)
            setSettings(value);
    }
}
