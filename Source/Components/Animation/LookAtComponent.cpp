#include "Pch.h"
#include "Components\Animation\LookAtComponent.h"
#include <cmath>
#include <flatbuffers/flatbuffers.h>
#include <imgui.h>
#include "Animation\Runtime\AnimatorInstance.h"
#include "Animation\Runtime\LookAt.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Components\Animation\AnimatorComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\LookAtComponent_generated.h"

namespace Engine
{
    namespace
    {
        const auto lookAtType = ComponentRegistry::instance().registerType<LookAtComponent>(
            "Look At", false, false, true,
            [](const GameObject& object) -> std::string_view
            {
                return object.hasComponent<AnimatorComponent>()
                    ? std::string_view{} : "Add an Animator before adding Look At.";
            });

        bool finite(const Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool unit(const Vector3& v)
        {
            return finite(v) && std::abs(v.LengthSquared() - 1.0f) < 1.0e-3f;
        }

        bool weightValid(const float weight)
        {
            return std::isfinite(weight) && weight >= 0.0f && weight <= 1.0f;
        }

        bool valid(const LookAtSettings& settings)
        {
            const auto validName = [](const std::string& name)
                { return name.size() <= 1024 && name.find('\0') == std::string::npos; };
            return validName(settings.endJoint) && validName(settings.rootJoint)
                && finite(settings.target) && finite(settings.offset)
                && unit(settings.forward) && unit(settings.up)
                && std::abs(settings.forward.Dot(settings.up)) < 1.0e-3f
                && weightValid(settings.weight) && weightValid(settings.jointWeight);
        }

        Serialization::Vec3 pack(const Vector3& v) { return { v.x, v.y, v.z }; }
        Vector3 unpack(const Serialization::Vec3& v) { return { v.x(), v.y(), v.z() }; }
    }

    LookAtComponent::LookAtComponent() noexcept
    {
        GE_UNUSED(lookAtType);
    }

    void LookAtComponent::resetRuntime() noexcept
    {
        m_resolvedSkeleton.reset();
        m_chain.clear();
        m_errorLogged = false;
        m_status.clear();
    }

    void LookAtComponent::onDisable() { resetRuntime(); }

    bool LookAtComponent::fail(const char* reason)
    {
        if (!m_errorLogged)
        {
            LOG_WARNING("[LookAt] {}", reason);
            m_status = reason;
            m_errorLogged = true;
        }
        return false;
    }

    bool LookAtComponent::setSettings(const LookAtSettings& settings)
    {
        if (!valid(settings))
        {
            LOG_ERROR("[LookAt] Invalid settings; keeping the previous configuration.");
            return false;
        }
        m_settings = settings;
        resetRuntime();
        return true;
    }

    bool LookAtComponent::setTarget(const Vector3& target)
    {
        if (!finite(target))
        {
            LOG_ERROR("[LookAt] Target must be finite.");
            return false;
        }
        m_settings.target = target;
        return true;
    }

    bool LookAtComponent::setWeight(const float weight)
    {
        if (!weightValid(weight))
        {
            LOG_ERROR("[LookAt] Weight must be finite and in [0,1].");
            return false;
        }
        m_settings.weight = weight;
        return true;
    }

    bool LookAtComponent::resolveRig(const SkeletonAsset& skeleton)
    {
        const auto end = skeleton.jointLookup.find(m_settings.endJoint);
        const auto root = skeleton.jointLookup.find(m_settings.rootJoint);
        if (end == skeleton.jointLookup.end() || root == skeleton.jointLookup.end())
            return fail("Select valid end and root joints in the Look At Inspector.");
        const auto parents = skeleton.skeleton.joint_parents();
        for (int joint = static_cast<int>(end->second); joint != ozz::animation::Skeleton::kNoParent;
            joint = parents[static_cast<std::size_t>(joint)])
        {
            m_chain.push_back(joint);
            if (joint == static_cast<int>(root->second))
                return true;
        }
        m_chain.clear();
        return fail("Root joint must be an ancestor of the end joint.");
    }

    bool LookAtComponent::evaluate(AnimatorInstance& animator, const bool hasFootIK)
    {
        if (m_settings.weight == 0.0f)
            return false;
        const auto* object = getGameObject();
        const auto skeleton = animator.getSkeletonAsset();
        if (!object || !skeleton)
            return fail("Look At requires a bound Animator.");
        if (m_resolvedSkeleton != skeleton)
        {
            resetRuntime();
            m_resolvedSkeleton = skeleton;
            if (!resolveRig(*skeleton))
                return false;
        }
        if (m_chain.empty())
            return false;
        LookAtTarget target;
        target.position = m_settings.target;
        target.forward = m_settings.forward;
        target.up = m_settings.up;
        target.offset = m_settings.offset;
        target.weight = m_settings.weight;
        target.jointWeight = m_settings.jointWeight;
        if (!animator.applyLookAt(m_chain, target, object->getWorldMatrix(), hasFootIK))
            return fail("Aim evaluation failed. Check the target, joint transforms and object scale.");
        m_errorLogged = false;
        m_status.clear();
        return true;
    }

    std::uint32_t LookAtComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_LOOK_AT_COMPONENT_VERSION;
    }

    bool LookAtComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto target = pack(m_settings.target);
        const auto forward = pack(m_settings.forward);
        const auto up = pack(m_settings.up);
        const auto offset = pack(m_settings.offset);
        const auto root = Serialization::CreateLookAtComponentPayload(builder,
            builder.CreateString(m_settings.endJoint), builder.CreateString(m_settings.rootJoint),
            &target, &forward, &up, &offset, m_settings.weight, m_settings.jointWeight);
        Serialization::FinishLookAtComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool LookAtComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != getPayloadVersion() || payload.size() < 8
            || !Serialization::VerifyLookAtComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[LookAt] Invalid payload or unsupported version.");
            return false;
        }
        const auto* source = Serialization::GetLookAtComponentPayload(payload.data());
        if (!source->target() || !source->forward() || !source->up() || !source->offset())
        {
            LOG_ERROR("[LookAt] Payload is missing target or axes.");
            return false;
        }
        LookAtSettings settings;
        settings.endJoint = source->end_joint()->str();
        settings.rootJoint = source->root_joint()->str();
        settings.target = unpack(*source->target());
        settings.forward = unpack(*source->forward());
        settings.up = unpack(*source->up());
        settings.offset = unpack(*source->offset());
        settings.weight = source->weight();
        settings.jointWeight = source->joint_weight();
        return setSettings(settings);
    }

    void LookAtComponent::onImGui()
    {
        const auto* object = getGameObject();
        const auto* animator = object ? object->getComponent<AnimatorComponent>() : nullptr;
        const auto skeleton = animator
            ? AnimationAssetManager::instance().getSkeleton(animator->getSkeleton()) : nullptr;
        if (!skeleton)
            ImGui::TextWrapped("Bind an Animator skeleton to select Look At joints.");
        LookAtSettings settings = m_settings;
        bool changed = false;
        const auto selectJoint = [&skeleton](const char* label, std::string& selected)
        {
            bool edited = false;
            if (ImGui::BeginCombo(label, selected.empty() ? "(none)" : selected.c_str()))
            {
                if (ImGui::Selectable("(none)", selected.empty()))
                {
                    selected.clear();
                    edited = true;
                }
                if (skeleton)
                    for (const auto& name : skeleton->jointNames)
                        if (ImGui::Selectable(name.c_str(), selected == name))
                        {
                            selected = name;
                            edited = true;
                        }
                ImGui::EndCombo();
            }
            return edited;
        };
        const auto selectAxis = [](const char* label, Vector3& value)
        {
            constexpr const char* names[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
            const std::array axes{ Vector3::UnitX, -Vector3::UnitX, Vector3::UnitY,
                -Vector3::UnitY, Vector3::UnitZ, -Vector3::UnitZ };
            int current = -1;
            for (int i = 0; i < 6; ++i)
                if (Vector3::DistanceSquared(value, axes[static_cast<std::size_t>(i)]) < 1.0e-6f)
                    current = i;
            bool edited = false;
            if (ImGui::BeginCombo(label, current < 0 ? "(custom)" : names[current]))
            {
                for (int i = 0; i < 6; ++i)
                    if (ImGui::Selectable(names[i], current == i))
                    {
                        value = axes[static_cast<std::size_t>(i)];
                        edited = true;
                    }
                ImGui::EndCombo();
            }
            return edited;
        };
        changed |= selectJoint("End joint", settings.endJoint);
        changed |= selectJoint("Root joint", settings.rootJoint);
        changed |= ImGui::DragFloat3("Target (world)", &settings.target.x, 0.01f);
        changed |= ImGui::DragFloat3("Eye offset (end local)", &settings.offset.x, 0.005f);
        const Vector3 oldForward = settings.forward;
        if (selectAxis("Forward (end local)", settings.forward))
        {
            if (std::abs(settings.forward.Dot(settings.up)) > 1.0e-3f)
                settings.up = oldForward;
            changed = true;
        }
        const Vector3 oldUp = settings.up;
        if (selectAxis("Up (joint local)", settings.up))
        {
            if (std::abs(settings.forward.Dot(settings.up)) > 1.0e-3f)
                settings.forward = oldUp;
            changed = true;
        }
        changed |= ImGui::SliderFloat("Weight", &settings.weight, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Joint weight", &settings.jointWeight, 0.0f, 1.0f);
        if (changed)
            setSettings(settings);
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::TextWrapped("Root must be an ancestor of End. Configure perpendicular local axes for the rig.");
    }
}
