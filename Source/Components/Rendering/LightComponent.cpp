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
            || value.atmosphereSunIndex > 1)
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
        const auto root = Serialization::CreateLightComponentPayload(builder, static_cast<std::uint8_t>(m_settings.kind),
            &color, m_settings.intensity, m_settings.range, m_settings.innerAngle, m_settings.outerAngle,
            m_settings.castsShadow, m_settings.shadowDistance, m_settings.shadowBias, m_settings.normalBias,
            m_settings.atmosphereSunLight, m_settings.atmosphereSunIndex);
        Serialization::FinishLightComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool LightComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if ((version != 1 && version != 2) || payload.size() < 8
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
        return setSettings({ static_cast<LightKind>(source->kind()),
            Vector3(source->color()->x(), source->color()->y(), source->color()->z()), source->intensity(),
            source->range(), source->inner_angle(), source->outer_angle(), source->casts_shadow(),
            source->shadow_distance(), source->shadow_bias(), source->normal_bias(),
            version >= 2 && source->atmosphere_sun_light(),
            static_cast<std::uint8_t>(version >= 2 ? source->atmosphere_sun_index() : 0) });
    }

    void LightComponent::onImGui()
    {
        LightSettings value = m_settings;
        int kind = static_cast<int>(value.kind);
        bool changed = ImGui::Combo("Kind", &kind, "Directional\0Point\0Spot\0");
        value.kind = static_cast<LightKind>(kind);
        changed |= ImGui::ColorEdit3("Color", &value.color.x);
        changed |= ImGui::DragFloat("Intensity", &value.intensity, 0.05f, 0, 100000);
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
        }
        if (changed)
            setSettings(value);
    }
}
