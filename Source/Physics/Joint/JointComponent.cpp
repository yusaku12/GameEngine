#include "Pch.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Physics\Joint\JointComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Rigidbody\RigidbodyComponent.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PhysicsComponent_generated.h"
#include "Physics\Internal\PhysicsPayload.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        bool isFiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    void JointComponent::setType(const PhysicsJointType type) noexcept
    {
        if (static_cast<std::uint8_t>(type) > static_cast<std::uint8_t>(PhysicsJointType::D6))
        {
            LOG_ERROR("[JointComponent] Invalid joint type.");
            return;
        }
        if (m_type == type)
            return;
        m_type = type;
        refreshJoint();
    }

    void JointComponent::setConnectedBody(RigidbodyComponent* const body) noexcept
    {
        GameObject* const gameObject = getGameObject();
        if (body != nullptr && gameObject != nullptr
            && body == gameObject->getComponent<RigidbodyComponent>())
        {
            LOG_ERROR("[JointComponent] A Rigidbody cannot be connected to itself.");
            return;
        }
        if (m_connectedBody == body)
            return;
        m_connectedBody = body;
        refreshJoint();
    }

    void JointComponent::setLocalAnchor(const Vector3& anchor) noexcept
    {
        if (!isFiniteVector(anchor))
        {
            LOG_ERROR("[JointComponent] Local anchor must be finite.");
            return;
        }
        if (m_localAnchor == anchor)
            return;
        m_localAnchor = anchor;
        refreshJoint();
    }

    void JointComponent::setConnectedAnchor(const Vector3& anchor) noexcept
    {
        if (!isFiniteVector(anchor))
        {
            LOG_ERROR("[JointComponent] Connected anchor must be finite.");
            return;
        }
        if (m_connectedAnchor == anchor)
            return;
        m_connectedAnchor = anchor;
        refreshJoint();
    }

    void JointComponent::setBreakForce(const float force, const float torque) noexcept
    {
        if (!std::isfinite(force) || !std::isfinite(torque) || force <= 0.0f || torque <= 0.0f)
        {
            LOG_ERROR("[JointComponent] Break force and torque must be positive finite values.");
            return;
        }
        m_breakForce = force;
        m_breakTorque = torque;
        refreshJoint();
    }

    void JointComponent::setCollisionEnabled(const bool enabled) noexcept
    {
        if (m_collisionEnabled == enabled)
            return;
        m_collisionEnabled = enabled;
        refreshJoint();
    }

    bool JointComponent::setDistanceRange(const float minimum, const float maximum) noexcept
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum)
            || minimum < 0.0f || maximum < minimum)
        {
            LOG_ERROR("[JointComponent] Distance range must be finite, non-negative, and ordered.");
            return false;
        }
        m_distanceMinimum = minimum;
        m_distanceMaximum = maximum;
        m_distanceRangeSet = true;
        refreshJoint();
        return true;
    }

    void JointComponent::clearDistanceRange() noexcept
    {
        if (!m_distanceRangeSet)
            return;
        m_distanceRangeSet = false;
        refreshJoint();
    }

    void JointComponent::setD6AxisFree(const PhysicsJointAxis axis, const bool free) noexcept
    {
        const auto axisIndex = static_cast<std::uint8_t>(axis);
        if (axisIndex >= m_d6FreeAxes.size())
        {
            LOG_ERROR("[JointComponent] Invalid D6 axis.");
            return;
        }
        if (m_d6FreeAxes[axisIndex] == free)
            return;
        m_d6FreeAxes[axisIndex] = free;
        refreshJoint();
    }

    bool JointComponent::isD6AxisFree(const PhysicsJointAxis axis) const noexcept
    {
        const auto axisIndex = static_cast<std::uint8_t>(axis);
        return axisIndex < m_d6FreeAxes.size() && m_d6FreeAxes[axisIndex];
    }

    bool JointComponent::setRevoluteLimit(const float minimum, const float maximum) noexcept
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum)
            || minimum < -3.14159265358979323846f
            || maximum > 3.14159265358979323846f
            || minimum > maximum)
        {
            LOG_ERROR("[JointComponent] Revolute limits must be ordered within [-pi, pi].");
            return false;
        }
        m_revoluteMinimum = minimum;
        m_revoluteMaximum = maximum;
        m_revoluteLimitSet = true;
        refreshJoint();
        return true;
    }

    void JointComponent::clearRevoluteLimit() noexcept
    {
        if (!m_revoluteLimitSet)
            return;
        m_revoluteLimitSet = false;
        refreshJoint();
    }

    std::uint32_t JointComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_PHYSICS_COMPONENT_VERSION;
    }

    bool JointComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder(160);
        Serialization::AssetGuid connectedGuid;
        const GameObject* connectedObject = m_connectedBody != nullptr
            ? m_connectedBody->getGameObject() : nullptr;
        const ObjectGUID guid = connectedObject != nullptr
            ? connectedObject->getGUID() : m_pendingConnectedBodyGuid;
        const Serialization::AssetGuid* connectedGuidPointer = nullptr;
        if (guid.isValid())
        {
            connectedGuid = Serialization::AssetGuid(guid.high, guid.low);
            connectedGuidPointer = &connectedGuid;
        }
        const Serialization::Vec3 localAnchor(m_localAnchor.x, m_localAnchor.y, m_localAnchor.z);
        const Serialization::Vec3 connectedAnchor(
            m_connectedAnchor.x, m_connectedAnchor.y, m_connectedAnchor.z);
        std::uint8_t freeAxes = 0;
        for (std::size_t index = 0; index < m_d6FreeAxes.size(); ++index)
        {
            if (m_d6FreeAxes[index])
                freeAxes |= static_cast<std::uint8_t>(1u << index);
        }
        const auto root = Serialization::CreateJointComponentPayload(builder,
            static_cast<std::uint8_t>(m_type), connectedGuidPointer, &localAnchor, &connectedAnchor,
            m_breakForce, m_breakTorque, m_distanceMinimum, m_distanceMaximum,
            m_revoluteMinimum, m_revoluteMaximum, freeAxes, m_distanceRangeSet,
            m_revoluteLimitSet, m_collisionEnabled);
        PhysicsSerialization::writePayload(builder, root, "JTCP", payload);
        return true;
    }

    bool JointComponent::deserializePayload(
        const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        if (version == 0 && payload.empty())
            return true;
        const Serialization::JointComponentPayload* source = nullptr;
        if (version < Serialization::MINIMUM_SUPPORTED_PHYSICS_COMPONENT_VERSION
            || version > Serialization::CURRENT_PHYSICS_COMPONENT_VERSION
            || !PhysicsSerialization::readPayload(payload, "JTCP", source)
            || source->type() > static_cast<std::uint8_t>(PhysicsJointType::D6)
            || source->local_anchor() == nullptr || source->connected_anchor() == nullptr
            || !std::isfinite(source->break_force()) || source->break_force() <= 0.0f
            || !std::isfinite(source->break_torque()) || source->break_torque() <= 0.0f
            || (source->d6_free_axes() & 0xC0u) != 0)
            return false;

        const Vector3 localAnchor(source->local_anchor()->x(), source->local_anchor()->y(),
            source->local_anchor()->z());
        const Vector3 connectedAnchor(source->connected_anchor()->x(), source->connected_anchor()->y(),
            source->connected_anchor()->z());
        if (!isFiniteVector(localAnchor) || !isFiniteVector(connectedAnchor)
            || (source->distance_range_set()
                && (!std::isfinite(source->distance_minimum())
                    || !std::isfinite(source->distance_maximum())
                    || source->distance_minimum() < 0.0f
                    || source->distance_maximum() < source->distance_minimum()))
            || (source->revolute_limit_set()
                && (!std::isfinite(source->revolute_minimum())
                    || !std::isfinite(source->revolute_maximum())
                    || source->revolute_minimum() < -3.14159265358979323846f
                    || source->revolute_maximum() > 3.14159265358979323846f
                    || source->revolute_minimum() > source->revolute_maximum())))
            return false;

        setConnectedBody(nullptr);
        const auto* connectedGuid = source->connected_body_guid();
        m_pendingConnectedBodyGuid = connectedGuid == nullptr
            ? ObjectGUID{} : ObjectGUID{ connectedGuid->high(), connectedGuid->low() };
        setType(static_cast<PhysicsJointType>(source->type()));
        setLocalAnchor(localAnchor);
        setConnectedAnchor(connectedAnchor);
        setBreakForce(source->break_force(), source->break_torque());
        setCollisionEnabled(source->collision_enabled());
        if (source->distance_range_set())
            setDistanceRange(source->distance_minimum(), source->distance_maximum());
        else
            clearDistanceRange();
        if (source->revolute_limit_set())
            setRevoluteLimit(source->revolute_minimum(), source->revolute_maximum());
        else
            clearRevoluteLimit();
        for (std::uint8_t index = 0; index < 6; ++index)
            setD6AxisFree(static_cast<PhysicsJointAxis>(index),
                (source->d6_free_axes() & (1u << index)) != 0);
        return true;
    }

    bool JointComponent::resolveObjectReferences(const ObjectReferenceMap& objects)
    {
        if (!m_pendingConnectedBodyGuid.isValid())
            return true;
        const auto found = objects.find(m_pendingConnectedBodyGuid);
        if (found == objects.end() || found->second == nullptr)
        {
            LOG_ERROR("[JointComponent] Serialized connected Rigidbody GameObject could not be found.");
            return false;
        }
        RigidbodyComponent* const connectedBody = found->second->getComponent<RigidbodyComponent>();
        if (connectedBody == nullptr || found->second == getGameObject())
        {
            LOG_ERROR("[JointComponent] Serialized connected GameObject has no distinct Rigidbody.");
            return false;
        }
        setConnectedBody(connectedBody);
        m_pendingConnectedBodyGuid = {};
        return true;
    }

    void JointComponent::onImGui()
    {
        constexpr const char* jointTypeNames[] = { "Fixed", "Revolute", "Distance", "D6" };
        int jointType = static_cast<int>(getType());
        if (ImGui::Combo("Type", &jointType, jointTypeNames, static_cast<int>(std::size(jointTypeNames))))
            setType(static_cast<PhysicsJointType>(jointType));

        const GameObject* const connectedObject =
            m_connectedBody != nullptr ? m_connectedBody->getGameObject() : nullptr;
        const char* preview = connectedObject != nullptr ? connectedObject->getName().c_str() : "World";
        if (ImGui::BeginCombo("Connected Body", preview))
        {
            if (ImGui::Selectable("World", m_connectedBody == nullptr))
                setConnectedBody(nullptr);
            Scene* const scene = SceneManager::instance().getActiveScene();
            if (scene != nullptr)
            {
                for (const auto& object : scene->getGameObjects())
                {
                    if (object == nullptr || object.get() == getGameObject()
                        || !object->isActiveInHierarchy())
                        continue;
                    RigidbodyComponent* const rigidbody = object->getComponent<RigidbodyComponent>();
                    if (rigidbody == nullptr)
                        continue;
                    ImGui::PushID(object.get());
                    if (ImGui::Selectable(object->getName().c_str(), rigidbody == m_connectedBody))
                        setConnectedBody(rigidbody);
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }

        Vector3 localAnchor = getLocalAnchor();
        if (ImGui::DragFloat3("Local Anchor", &localAnchor.x, 0.05f))
            setLocalAnchor(localAnchor);
        Vector3 connectedAnchor = getConnectedAnchor();
        if (ImGui::DragFloat3("Connected Anchor", &connectedAnchor.x, 0.05f))
            setConnectedAnchor(connectedAnchor);

        float breakForce = getBreakForce();
        if (ImGui::DragFloat("Break Force", &breakForce, 1.0f, 0.001f, 100000000.0f))
            setBreakForce(breakForce, getBreakTorque());
        float breakTorque = getBreakTorque();
        if (ImGui::DragFloat("Break Torque", &breakTorque, 1.0f, 0.001f, 100000000.0f))
            setBreakForce(getBreakForce(), breakTorque);
        bool collisionEnabled = isCollisionEnabled();
        if (ImGui::Checkbox("Enable Collision", &collisionEnabled))
            setCollisionEnabled(collisionEnabled);

        if (getType() == PhysicsJointType::Distance)
        {
            bool rangeEnabled = hasDistanceRange();
            if (ImGui::Checkbox("Enable Distance Range", &rangeEnabled))
            {
                if (rangeEnabled)
                    setDistanceRange(0.0f, 0.0f);
                else
                    clearDistanceRange();
            }
            if (hasDistanceRange())
            {
                float minimum = 0.0f;
                float maximum = 0.0f;
                getDistanceRange(minimum, maximum);
                if (ImGui::DragFloat("Minimum Distance", &minimum, 0.05f, 0.0f, 100000.0f))
                    setDistanceRange(minimum, maximum);
                if (ImGui::DragFloat("Maximum Distance", &maximum, 0.05f, 0.0f, 100000.0f))
                    setDistanceRange(minimum, maximum);
            }
        }
        else if (getType() == PhysicsJointType::Revolute)
        {
            bool limitEnabled = hasRevoluteLimit();
            if (ImGui::Checkbox("Enable Limit", &limitEnabled))
            {
                if (limitEnabled)
                    setRevoluteLimit(0.0f, 0.0f);
                else
                    clearRevoluteLimit();
            }
            if (hasRevoluteLimit())
            {
                float minimum = 0.0f;
                float maximum = 0.0f;
                getRevoluteLimit(minimum, maximum);
                minimum = DirectX::XMConvertToDegrees(minimum);
                maximum = DirectX::XMConvertToDegrees(maximum);
                if (ImGui::DragFloat("Minimum Angle", &minimum, 1.0f, -180.0f, 180.0f)
                    || ImGui::DragFloat("Maximum Angle", &maximum, 1.0f, -180.0f, 180.0f))
                    setRevoluteLimit(DirectX::XMConvertToRadians(minimum), DirectX::XMConvertToRadians(maximum));
            }
        }
        else if (getType() == PhysicsJointType::D6)
        {
            constexpr const char* axisNames[] = { "X", "Y", "Z", "Twist", "Swing 1", "Swing 2" };
            for (std::uint8_t index = 0; index < 6; ++index)
            {
                bool free = isD6AxisFree(static_cast<PhysicsJointAxis>(index));
                if (ImGui::Checkbox(axisNames[index], &free))
                    setD6AxisFree(static_cast<PhysicsJointAxis>(index), free);
            }
        }
    }

    void JointComponent::onPayloadRestored()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->createJoint(*this))
                LOG_ERROR("[JointComponent] Failed to recreate the Joint after payload restoration.");
        }
    }

    void JointComponent::onEnable()
    {
        PhysicsWorld* const world = PhysicsSystem::instance().getWorld();
        if (world == nullptr || !world->createJoint(*this))
            LOG_ERROR("[JointComponent] Failed to create the Physics Joint.");
    }

    void JointComponent::onDisable()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->destroyJoint(*this);
    }

    void JointComponent::onDestroy()
    {
        onDisable();
    }

    void JointComponent::refreshJoint() noexcept
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateJoint(*this);
    }
} // namespace Engine
