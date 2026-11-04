#include "Pch.h"
#include "Components\Rendering\PostProcessVolumeComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Editor\PostProcess\PostProcessInspector.h"
#include "Generated\FlatBuffers\PostProcessVolumeComponent_generated.h"
#include <cmath>
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const auto volumeType = ComponentRegistry::instance().registerType<PostProcessVolumeComponent>(
            "PostProcessVolume", false, false, false);
        bool finite(const Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    bool validatePostProcessVolumeSettings(const PostProcessVolumeSettings& settings) noexcept
    {
        if (settings.shape > PostProcessVolumeShape::Sphere
            || !std::isfinite(settings.priority) || !std::isfinite(settings.weight)
            || settings.weight < 0 || settings.weight > 1
            || !std::isfinite(settings.blendDistance) || settings.blendDistance < 0
            || !finite(settings.center) || !finite(settings.boxSize)
            || settings.boxSize.x <= 0 || settings.boxSize.y <= 0 || settings.boxSize.z <= 0
            || !std::isfinite(settings.sphereRadius) || settings.sphereRadius <= 0)
        {
            LOG_ERROR("[PostProcessVolume] Invalid shape/priority/weight/geometry.");
            return false;
        }
        return true;
    }

    bool PostProcessVolumeComponent::setSettings(const PostProcessVolumeSettings& settings) noexcept
    {
        if (!validatePostProcessVolumeSettings(settings))
            return false;
        m_settings = settings;
        m_invalidTransformReported = false;
        return true;
    }

    bool PostProcessVolumeComponent::setPostProcessProfile(const PostProcessProfileHandle profile)
    {
        if (profile != PostProcessProfileHandle{} && !PostProcessProfileManager::instance().get(profile))
        {
            LOG_ERROR("[PostProcessVolume] Cannot assign an invalid Profile handle.");
            return false;
        }
        m_profile = profile;
        m_missingProfileReported = false;
        return true;
    }

    std::shared_ptr<const PostProcessProfile> PostProcessVolumeComponent::profileSnapshot() const
    {
        const auto profile = PostProcessProfileManager::instance().get(m_profile);
        if (m_profile.isValid() && !profile && !m_missingProfileReported)
        {
            LOG_ERROR("[PostProcessVolume] Stale Profile handle; this Volume contributes nothing.");
            m_missingProfileReported = true;
        }
        return profile;
    }

    float PostProcessVolumeComponent::influence(const Vector3& position) const noexcept
    {
        if (m_settings.weight == 0)
            return 0;
        if (!finite(position))
        {
            LOG_ERROR("[PostProcessVolume] Camera position must be finite.");
            return 0;
        }
        if (m_settings.shape == PostProcessVolumeShape::Global)
            return m_settings.weight;
        if (!getGameObject())
        {
            LOG_ERROR("[PostProcessVolume] Local Volume has no GameObject transform.");
            return 0;
        }
        const auto& transform = getGameObject()->getWorldTransform();
        const auto scale = transform.getScale();
        auto rotation = transform.getRotation();
        const auto center = transform.transformPoint(m_settings.center);
        const Vector3 size(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
        const double quaternionLength = static_cast<double>(rotation.x) * rotation.x
            + static_cast<double>(rotation.y) * rotation.y + static_cast<double>(rotation.z) * rotation.z
            + static_cast<double>(rotation.w) * rotation.w;
        const auto invalid = [this]()
        {
            if (!m_invalidTransformReported)
            {
                LOG_ERROR("[PostProcessVolume] Invalid or degenerate world transform; local Volume skipped.");
                m_invalidTransformReported = true;
            }
            return 0.0f;
        };
        if (!finite(center) || !finite(size) || size.x == 0 || size.y == 0 || size.z == 0
            || !std::isfinite(quaternionLength) || quaternionLength < 0.00000001)
            return invalid();
        rotation.Normalize();
        const Vector3 relative = position - center;
        if (!finite(relative))
            return invalid();
        double distance = 0;
        if (m_settings.shape == PostProcessVolumeShape::Sphere)
        {
            const double radius = static_cast<double>(m_settings.sphereRadius) * std::max({ size.x, size.y, size.z });
            distance = std::max(0.0, std::hypot(static_cast<double>(relative.x),
                static_cast<double>(relative.y), static_cast<double>(relative.z)) - radius);
        }
        else
        {
            Quaternion inverse;
            rotation.Conjugate(inverse);
            const auto local = Vector3::Transform(relative, inverse);
            const double dx = std::max(0.0, std::abs(static_cast<double>(local.x)) - static_cast<double>(m_settings.boxSize.x) * size.x * 0.5);
            const double dy = std::max(0.0, std::abs(static_cast<double>(local.y)) - static_cast<double>(m_settings.boxSize.y) * size.y * 0.5);
            const double dz = std::max(0.0, std::abs(static_cast<double>(local.z)) - static_cast<double>(m_settings.boxSize.z) * size.z * 0.5);
            distance = std::hypot(dx, dy, dz);
        }
        if (!std::isfinite(distance))
            return invalid();
        m_invalidTransformReported = false;
        if (distance == 0)
            return m_settings.weight;
        if (m_settings.blendDistance == 0 || distance >= m_settings.blendDistance)
            return 0;
        const double t = 1 - distance / m_settings.blendDistance;
        return static_cast<float>(m_settings.weight * t * t * (3 - 2 * t));
    }

    bool PostProcessVolumeComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        Serialization::PostProcessProfileReference reference;
        if (!Serialization::writePostProcessProfileReference(m_profile, reference))
            return false;
        flatbuffers::FlatBufferBuilder builder;
        const Serialization::Vec3 center(m_settings.center.x, m_settings.center.y, m_settings.center.z);
        const Serialization::Vec3 size(m_settings.boxSize.x, m_settings.boxSize.y, m_settings.boxSize.z);
        const Serialization::AssetGuid guid(reference.guid.high, reference.guid.low);
        const auto root = Serialization::CreatePostProcessVolumeComponentPayload(builder,
            static_cast<std::uint8_t>(m_settings.shape), m_settings.priority, m_settings.weight, m_settings.blendDistance,
            &center, &size, m_settings.sphereRadius, &guid, builder.CreateString(reference.path));
        Serialization::FinishPostProcessVolumeComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool PostProcessVolumeComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != 1 || payload.size() < 8 || !Serialization::VerifyPostProcessVolumeComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[PostProcessVolume] Invalid Volume payload/version.");
            return false;
        }
        const auto* const source = Serialization::GetPostProcessVolumeComponentPayload(payload.data());
        if (!source->center() || !source->box_size() || !source->profile_guid() || !source->profile_path())
        {
            LOG_ERROR("[PostProcessVolume] Missing Volume geometry or Profile reference.");
            return false;
        }
        const PostProcessVolumeSettings settings{ static_cast<PostProcessVolumeShape>(source->shape()),
            source->priority(), source->weight(), source->blend_distance(),
            Vector3(source->center()->x(), source->center()->y(), source->center()->z()),
            Vector3(source->box_size()->x(), source->box_size()->y(), source->box_size()->z()), source->sphere_radius() };
        PostProcessProfileHandle handle;
        if (!validatePostProcessVolumeSettings(settings)
            || !Serialization::readPostProcessProfileReference({ { source->profile_guid()->high(), source->profile_guid()->low() },
                source->profile_path()->str() }, handle))
            return false;
        m_settings = settings;
        m_profile = handle;
        m_missingProfileReported = m_invalidTransformReported = false;
        return true;
    }

    bool PostProcessVolumeComponent::copyConfigurationTo(Component& destination) const
    {
        auto* const target = dynamic_cast<PostProcessVolumeComponent*>(&destination);
        if (!target)
        {
            LOG_ERROR("[PostProcessVolume] Clone component type mismatch.");
            return false;
        }
        target->m_settings = m_settings;
        target->m_profile = m_profile;
        target->m_missingProfileReported = target->m_invalidTransformReported = false;
        return true;
    }

    void PostProcessVolumeComponent::onImGui()
    {
        auto edited = m_settings;
        int shape = static_cast<int>(edited.shape);
        bool changed = ImGui::Combo("Shape", &shape, "Global\0Box\0Sphere\0");
        edited.shape = static_cast<PostProcessVolumeShape>(shape);
        changed |= ImGui::InputFloat("Priority", &edited.priority);
        changed |= ImGui::SliderFloat("Volume Weight", &edited.weight, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (edited.shape != PostProcessVolumeShape::Global)
        {
            changed |= ImGui::DragFloat("Blend Distance (World)", &edited.blendDistance, 0.1f, 0, 100000,
                "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat3("Center (Local)", &edited.center.x, 0.1f);
            if (edited.shape == PostProcessVolumeShape::Box)
                changed |= ImGui::DragFloat3("Box Size (Local)", &edited.boxSize.x, 0.1f, 0.001f, 100000,
                    "%.3f", ImGuiSliderFlags_AlwaysClamp);
            else
                changed |= ImGui::DragFloat("Radius (Local)", &edited.sphereRadius, 0.1f, 0.001f, 100000,
                    "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (changed)
            setSettings(edited);
        ImGui::TextDisabled("Priority ascending, then GUID. Outer shell fades smoothly. GameObject Layer filters cameras.");
        drawPostProcessVolumeInspector(*this);
    }
}
