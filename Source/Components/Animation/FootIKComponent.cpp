#include "Pch.h"
#include "Components\Animation\FootIKComponent.h"
#include <cmath>
#include <flatbuffers/flatbuffers.h>
#include <imgui.h>
#include "Animation\Runtime\AnimatorInstance.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Components\Animation\AnimatorComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\FootIKComponent_generated.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"

namespace Engine
{
    namespace
    {
        const auto footIKType = ComponentRegistry::instance().registerType<FootIKComponent>(
            "Foot IK", false, false, true,
            [](const GameObject& object) -> std::string_view
            {
                return object.hasComponent<AnimatorComponent>()
                    ? std::string_view{} : "Add an Animator before adding Foot IK.";
            });

        bool range(const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        }

        bool validAxis(const Vector3& axis)
        {
            return std::isfinite(axis.x) && std::isfinite(axis.y) && std::isfinite(axis.z)
                && std::abs(axis.LengthSquared() - 1.0f) < 1.0e-3f;
        }

        int resolveJoint(const SkeletonAsset& skeleton, const std::string& name)
        {
            const auto found = skeleton.jointLookup.find(name);
            return found != skeleton.jointLookup.end() ? static_cast<int>(found->second) : -1;
        }

        bool validSettings(const FootIKSettings& settings)
        {
            if (!range(settings.weight, 0.0f, 1.0f) || !range(settings.soften, 0.0f, 1.0f)
                || !range(settings.rayHeight, 0.0f, 10000.0f) || !range(settings.rayDepth, 0.0f, 10000.0f)
                || settings.rayHeight + settings.rayDepth <= 0.0f
                || !range(settings.maxAdjustment, 0.0f, 10000.0f)
                || !range(settings.maxSlope, 0.0f, 89.0f)
                || !range(settings.maxPelvisOffset, 0.0f, 10000.0f)
                || !range(settings.responseSpeed, 0.0f, 1000.0f))
                return false;
            for (const auto& leg : settings.legs)
            {
                if (!range(leg.weight, 0.0f, 1.0f) || !range(leg.footHeight, 0.0f, 10000.0f)
                    || !validAxis(leg.kneeAxis) || !validAxis(leg.poleAxis)
                    || !validAxis(leg.footUp) || !validAxis(leg.footForward)
                    || std::abs(leg.footUp.Dot(leg.footForward)) > 1.0e-3f)
                    return false;
                for (const std::string* name : { &leg.hip, &leg.knee, &leg.ankle })
                    if (name->size() > 1024 || name->find('\0') != std::string::npos)
                        return false;
            }
            return true;
        }

        Serialization::Vec3 pack(const Vector3& value)
        {
            return { value.x, value.y, value.z };
        }

        Vector3 unpack(const Serialization::Vec3& value)
        {
            return { value.x(), value.y(), value.z() };
        }
    }

    FootIKComponent::FootIKComponent() noexcept
    {
        GE_UNUSED(footIKType);
    }

    void FootIKComponent::resetRuntime() noexcept
    {
        m_resolvedSkeleton = nullptr;
        m_rigValid = false;
        m_errorLogged = false;
        m_contacts = {};
        m_pelvisOffset = 0.0f;
        m_status.clear();
    }

    void FootIKComponent::onDisable()
    {
        resetRuntime();
    }

    bool FootIKComponent::fail(const char* reason)
    {
        if (!m_errorLogged)
        {
            LOG_WARNING("[FootIK] {}", reason);
            m_status = reason;
            m_errorLogged = true;
        }
        return false;
    }

    bool FootIKComponent::setSettings(const FootIKSettings& settings)
    {
        if (!validSettings(settings))
        {
            LOG_ERROR("[FootIK] Invalid configuration; keeping previous settings.");
            return false;
        }
        m_settings = settings;
        resetRuntime();
        return true;
    }

    bool FootIKComponent::resolveRig(const SkeletonAsset& skeleton)
    {
        m_rigValid = false;
        std::array<FootIKTarget, 2> configured;
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_settings.legs.size(); ++index)
        {
            const auto& leg = m_settings.legs[index];
            m_rig[index] = {};
            if (leg.hip.empty() && leg.knee.empty() && leg.ankle.empty())
                continue;
            auto& target = m_rig[index];
            target.hip = resolveJoint(skeleton, leg.hip);
            target.knee = resolveJoint(skeleton, leg.knee);
            target.ankle = resolveJoint(skeleton, leg.ankle);
            target.kneeAxis = leg.kneeAxis;
            target.poleAxis = leg.poleAxis;
            target.footUp = leg.footUp;
            target.footForward = leg.footForward;
            configured[count++] = target;
        }
        if (count == 0)
            return fail("Select hip, knee and ankle joints in the Foot IK Inspector.");
        if (!anchorFootIKPolesToHipParents(skeleton.skeleton, std::span(configured).first(count)))
            return fail("Invalid rig. Select independent hip/knee/ankle chains and check local axes.");
        std::size_t resolved = 0;
        for (auto& target : m_rig)
            if (target.hip >= 0)
                target = configured[resolved++];
        m_rigValid = true;
        return true;
    }

    bool FootIKComponent::setWeight(const float weight)
    {
        if (!range(weight, 0.0f, 1.0f))
        {
            LOG_ERROR("[FootIK] Weight must be finite and in [0,1].");
            return false;
        }
        m_settings.weight = weight;
        return true;
    }

    bool FootIKComponent::setLegWeight(const std::size_t leg, const float weight)
    {
        if (leg >= m_settings.legs.size() || !range(weight, 0.0f, 1.0f))
        {
            LOG_ERROR("[FootIK] Leg index or weight is invalid.");
            return false;
        }
        m_settings.legs[leg].weight = weight;
        return true;
    }

    bool FootIKComponent::evaluate(AnimatorInstance& animator, const float deltaTime)
    {
        const GameObject* const object = getGameObject();
        const auto skeleton = animator.getSkeletonAsset();
        const PhysicsWorld* const physics = PhysicsSystem::instance().getWorld();
        if (!object || !skeleton || !physics)
            return fail("Foot IK requires a bound Animator and an initialized PhysicsWorld.");
        if (!std::isfinite(deltaTime))
            return fail("Foot IK delta time must be finite.");
        if (m_resolvedSkeleton != skeleton)
        {
            resetRuntime();
            m_resolvedSkeleton = skeleton;
            if (!resolveRig(*skeleton))
                return false;
        }
        if (!m_rigValid)
            return false;
        const Matrix world = object->getWorldMatrix();
        PhysicsQueryFilter filter;
        filter.includeDynamic = m_settings.includeDynamic;
        filter.layerMask = m_settings.layerMask;
        filter.ignoredGameObject = object;
        const float blend = m_settings.responseSpeed == 0.0f ? 1.0f
            : -std::expm1(-m_settings.responseSpeed * std::abs(deltaTime));
        std::array<FootIKTarget, 2> targets;
        std::size_t count = 0;
        float desiredPelvis = 0.0f;
        for (std::size_t index = 0; index < m_rig.size(); ++index)
        {
            if (m_rig[index].ankle < 0)
                continue;
            Vector3 ankle;
            if (!animator.getJointWorldPosition(m_rig[index].ankle, world, ankle))
                return fail("Cannot read the animated ankle position.");
            const auto& leg = m_settings.legs[index];
            auto& contact = m_contacts[index];
            const auto hit = physics->raycast(ankle + Vector3::UnitY * m_settings.rayHeight,
                -Vector3::UnitY, m_settings.rayHeight + m_settings.rayDepth, filter);
            Vector3 ground;
            const bool grounded = hit && calculateFootIKGroundTarget(hit->point, hit->normal,
                leg.footHeight, m_settings.maxSlope, ground)
                && std::abs(ground.y - ankle.y) <= m_settings.maxAdjustment;
            const float desiredWeight = grounded ? leg.weight * m_settings.weight : 0.0f;
            contact.weight += (desiredWeight - contact.weight) * blend;
            if (grounded)
            {
                contact.offset += (ground.y - ankle.y - contact.offset) * blend;
                contact.normal = Vector3::Lerp(contact.normal, hit->normal, blend);
                contact.normal.Normalize();
            }
            if (contact.weight <= 1.0e-4f)
            {
                contact = {};
                continue;
            }
            auto& target = targets[count++];
            target = m_rig[index];
            target.position = ankle + Vector3::UnitY * contact.offset;
            target.normal = contact.normal;
            target.weight = contact.weight;
            target.soften = m_settings.soften;
            target.alignFoot = m_settings.alignFeet;
            desiredPelvis = (std::min)(desiredPelvis, contact.offset * contact.weight);
        }
        desiredPelvis = m_settings.pelvisCorrection
            ? (std::max)(desiredPelvis, -m_settings.maxPelvisOffset) : 0.0f;
        m_pelvisOffset += (desiredPelvis - m_pelvisOffset) * blend;
        if (!animator.applyFootIK(std::span(targets).first(count), Vector3::UnitY * m_pelvisOffset, world))
            return fail("IK evaluation failed. Check rig axes and positive uniform object scale.");
        m_status.clear();
        m_errorLogged = false;
        return true;
    }

    std::uint32_t FootIKComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_FOOT_IK_COMPONENT_VERSION;
    }

    bool FootIKComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        std::array<flatbuffers::Offset<Serialization::FootIKLeg>, 2> legs;
        for (std::size_t index = 0; index < legs.size(); ++index)
        {
            const auto& leg = m_settings.legs[index];
            const auto kneeAxis = pack(leg.kneeAxis);
            const auto poleAxis = pack(leg.poleAxis);
            const auto up = pack(leg.footUp);
            const auto forward = pack(leg.footForward);
            legs[index] = Serialization::CreateFootIKLeg(builder, builder.CreateString(leg.hip),
                builder.CreateString(leg.knee), builder.CreateString(leg.ankle),
                &kneeAxis, &poleAxis, &up, &forward, leg.weight, leg.footHeight);
        }
        const auto& settings = m_settings;
        const auto root = Serialization::CreateFootIKComponentPayload(builder, legs[0], legs[1],
            settings.weight, settings.soften, settings.rayHeight, settings.rayDepth,
            settings.maxAdjustment, settings.maxSlope, settings.pelvisCorrection, settings.maxPelvisOffset,
            settings.alignFeet, settings.responseSpeed, settings.layerMask, settings.includeDynamic);
        Serialization::FinishFootIKComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool FootIKComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != getPayloadVersion() || payload.size() < 8
            || !Serialization::VerifyFootIKComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[FootIK] Invalid payload or unsupported version.");
            return false;
        }
        const auto* source = Serialization::GetFootIKComponentPayload(payload.data());
        FootIKSettings settings;
        const std::array sources{ source->left(), source->right() };
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            const auto* leg = sources[index];
            if (!leg->hip() || !leg->knee() || !leg->ankle()
                || !leg->knee_axis() || !leg->pole_axis() || !leg->foot_up() || !leg->foot_forward())
            {
                LOG_ERROR("[FootIK] Payload is missing rig names or axes.");
                return false;
            }
            auto& target = settings.legs[index];
            target.hip = leg->hip()->str();
            target.knee = leg->knee()->str();
            target.ankle = leg->ankle()->str();
            target.kneeAxis = unpack(*leg->knee_axis());
            target.poleAxis = unpack(*leg->pole_axis());
            target.footUp = unpack(*leg->foot_up());
            target.footForward = unpack(*leg->foot_forward());
            target.weight = leg->weight();
            target.footHeight = leg->foot_height();
        }
        settings.weight = source->weight();
        settings.soften = source->soften();
        settings.rayHeight = source->ray_height();
        settings.rayDepth = source->ray_depth();
        settings.maxAdjustment = source->max_adjustment();
        settings.maxSlope = source->max_slope();
        settings.pelvisCorrection = source->pelvis_correction();
        settings.maxPelvisOffset = source->max_pelvis_offset();
        settings.alignFeet = source->align_feet();
        settings.responseSpeed = source->response_speed();
        settings.layerMask = source->layer_mask();
        settings.includeDynamic = source->include_dynamic();
        return setSettings(settings);
    }

    void FootIKComponent::onImGui()
    {
        const auto* object = getGameObject();
        const auto* animator = object ? object->getComponent<AnimatorComponent>() : nullptr;
        const auto skeleton = animator
            ? AnimationAssetManager::instance().getSkeleton(animator->getSkeleton()) : nullptr;
        if (!skeleton)
            ImGui::TextWrapped("Bind an Animator skeleton to select foot joints.");
        FootIKSettings settings = m_settings;
        bool changed = false;
        const auto jointSelector = [&skeleton](const char* label, std::string& selected)
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
        const auto axisSelector = [](const char* label, Vector3& value)
        {
            constexpr const char* names[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
            const std::array axes{ Vector3::UnitX, -Vector3::UnitX, Vector3::UnitY,
                -Vector3::UnitY, Vector3::UnitZ, -Vector3::UnitZ };
            int selected = -1;
            for (int index = 0; index < 6; ++index)
                if (Vector3::DistanceSquared(value, axes[static_cast<std::size_t>(index)]) < 1.0e-6f)
                    selected = index;
            bool edited = false;
            if (ImGui::BeginCombo(label, selected < 0 ? "(custom)" : names[selected]))
            {
                for (int index = 0; index < 6; ++index)
                    if (ImGui::Selectable(names[index], selected == index))
                    {
                        value = axes[static_cast<std::size_t>(index)];
                        edited = true;
                    }
                ImGui::EndCombo();
            }
            if (selected < 0)
                ImGui::TextDisabled("Local: (%.4f, %.4f, %.4f)", value.x, value.y, value.z);
            return edited;
        };
        for (std::size_t index = 0; index < settings.legs.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::TreeNode(index == 0 ? "Left Foot" : "Right Foot"))
            {
                auto& leg = settings.legs[index];
                changed |= jointSelector("Hip", leg.hip);
                changed |= jointSelector("Knee", leg.knee);
                changed |= jointSelector("Ankle", leg.ankle);
                changed |= axisSelector("Knee hinge axis", leg.kneeAxis);
                changed |= axisSelector("Knee pole axis", leg.poleAxis);
                changed |= axisSelector("Foot up axis", leg.footUp);
                changed |= axisSelector("Foot forward axis", leg.footForward);
                changed |= ImGui::SliderFloat("Leg weight", &leg.weight, 0.0f, 1.0f);
                changed |= ImGui::DragFloat("Sole height", &leg.footHeight, 0.005f, 0.0f, 10.0f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::BeginDisabled(!skeleton);
        if (ImGui::Button("Set axes from rest pose") && skeleton)
        {
            std::array<FootIKTarget, 2> targets;
            std::array<std::size_t, 2> legs{};
            std::size_t count = 0;
            for (std::size_t index = 0; index < settings.legs.size(); ++index)
            {
                const auto& leg = settings.legs[index];
                if (leg.hip.empty() && leg.knee.empty() && leg.ankle.empty())
                    continue;
                auto& target = targets[count];
                target.hip = resolveJoint(*skeleton, leg.hip);
                target.knee = resolveJoint(*skeleton, leg.knee);
                target.ankle = resolveJoint(*skeleton, leg.ankle);
                legs[count++] = index;
            }
            if (configureFootIKAxesFromRestPose(skeleton->skeleton, std::span(targets).first(count)))
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    auto& leg = settings.legs[legs[index]];
                    leg.kneeAxis = targets[index].kneeAxis;
                    leg.poleAxis = targets[index].poleAxis;
                    leg.footUp = targets[index].footUp;
                    leg.footForward = targets[index].footForward;
                }
                changed = true;
            }
            else
                m_status = "Rest-pose axis setup failed. Check the selected leg chains and the log.";
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Use a Y-up, +Z-forward neutral rest pose. Each leg is calibrated separately.");
        changed |= ImGui::SliderFloat("IK weight", &settings.weight, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Soften", &settings.soften, 0.0f, 1.0f);
        changed |= ImGui::Checkbox("Align feet to ground", &settings.alignFeet);
        changed |= ImGui::Checkbox("Visual pelvis correction", &settings.pelvisCorrection);
        changed |= ImGui::DragFloat("Max pelvis drop", &settings.maxPelvisOffset, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Ray height", &settings.rayHeight, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Ray depth", &settings.rayDepth, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Max foot adjustment", &settings.maxAdjustment, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::SliderFloat("Max ground slope", &settings.maxSlope, 0.0f, 89.0f);
        changed |= ImGui::DragFloat("Response speed (0 = instant)", &settings.responseSpeed, 0.5f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("Include dynamic ground", &settings.includeDynamic);
        changed |= ImGui::InputScalar("Ground layer mask", ImGuiDataType_U32, &settings.layerMask,
            nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        if (changed)
            setSettings(settings);
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::TextWrapped("World Y is up. Positive uniform world scale is required. "
            "Set leg weight to zero while a foot is airborne. Knee hinge direction is rig-specific.");
    }
}
