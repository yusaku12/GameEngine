#include "Pch.h"
#include "Core\GameObject\GameObject.h"
#include "Core\GameObject\Component\TransformComponent.h"
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
        bool isFiniteVector(const Vector3& value) noexcept;
    }

    void RigidbodyComponent::onImGui()
    {
        float mass = getMass();
        if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.001f, 100000.0f))
            setMass(mass);

        float linearDamping = getLinearDamping();
        if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 100.0f))
            setLinearDamping(linearDamping);

        float angularDamping = getAngularDamping();
        if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 100.0f))
            setAngularDamping(angularDamping);

        bool kinematic = isKinematic();
        if (ImGui::Checkbox("Kinematic", &kinematic))
            setKinematic(kinematic);

        bool gravityEnabled = isGravityEnabled();
        if (ImGui::Checkbox("Use Gravity", &gravityEnabled))
            setGravityEnabled(gravityEnabled);

        Vector3 linearVelocity = getLinearVelocity();
        if (ImGui::DragFloat3("Linear Velocity", &linearVelocity.x, 0.05f))
            setLinearVelocity(linearVelocity);

        Vector3 angularVelocity = getAngularVelocity();
        if (ImGui::DragFloat3("Angular Velocity", &angularVelocity.x, 0.05f))
            setAngularVelocity(angularVelocity);

        ImGui::BeginDisabled(isKinematic());
        if (ImGui::Button("Wake Up"))
            wakeUp();
        ImGui::SameLine();
        if (ImGui::Button("Sleep"))
            sleep();
        ImGui::EndDisabled();
    }

    std::uint32_t RigidbodyComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_PHYSICS_COMPONENT_VERSION;
    }

    bool RigidbodyComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder(128);
        const Serialization::Vec3 linearVelocity(m_linearVelocity.x, m_linearVelocity.y, m_linearVelocity.z);
        const Serialization::Vec3 angularVelocity(m_angularVelocity.x, m_angularVelocity.y, m_angularVelocity.z);
        const auto root = Serialization::CreateRigidbodyComponentPayload(builder, m_mass,
            &linearVelocity, &angularVelocity, m_linearDamping, m_angularDamping,
            m_isKinematic, m_gravityEnabled);
        PhysicsSerialization::writePayload(builder, root, "RBCP", payload);
        return true;
    }

    bool RigidbodyComponent::deserializePayload(
        const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        if (version == 0 && payload.empty())
            return true;
        const Serialization::RigidbodyComponentPayload* source = nullptr;
        if (version < Serialization::MINIMUM_SUPPORTED_PHYSICS_COMPONENT_VERSION
            || version > Serialization::CURRENT_PHYSICS_COMPONENT_VERSION
            || !PhysicsSerialization::readPayload(payload, "RBCP", source)
            || source->linear_velocity() == nullptr || source->angular_velocity() == nullptr)
            return false;

        const Vector3 linearVelocity(source->linear_velocity()->x(), source->linear_velocity()->y(),
            source->linear_velocity()->z());
        const Vector3 angularVelocity(source->angular_velocity()->x(), source->angular_velocity()->y(),
            source->angular_velocity()->z());
        if (!std::isfinite(source->mass()) || source->mass() <= 0.0f
            || !std::isfinite(source->linear_damping()) || source->linear_damping() < 0.0f
            || !std::isfinite(source->angular_damping()) || source->angular_damping() < 0.0f
            || !isFiniteVector(linearVelocity) || !isFiniteVector(angularVelocity))
            return false;

        setMass(source->mass());
        setLinearDamping(source->linear_damping());
        setAngularDamping(source->angular_damping());
        setLinearVelocity(linearVelocity);
        setAngularVelocity(angularVelocity);
        setKinematic(source->kinematic());
        setGravityEnabled(source->gravity_enabled());
        return true;
    }

    void RigidbodyComponent::onPayloadRestored()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->createRigidbody(*this))
                LOG_ERROR("[RigidbodyComponent] Failed to recreate the Rigidbody after payload restoration.");
        }
    }

    namespace
    {
        bool isFiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    void RigidbodyComponent::setMass(const float mass) noexcept
    {
        if (!std::isfinite(mass) || mass <= 0.0f)
        {
            LOG_ERROR("[RigidbodyComponent] Mass must be a positive finite value.");
            return;
        }
        m_mass = mass;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::setLinearVelocity(const Vector3& velocity) noexcept
    {
        if (!isFiniteVector(velocity))
        {
            LOG_ERROR("[RigidbodyComponent] Linear velocity must be finite.");
            return;
        }
        m_linearVelocity = velocity;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::setAngularVelocity(const Vector3& velocity) noexcept
    {
        if (!isFiniteVector(velocity))
        {
            LOG_ERROR("[RigidbodyComponent] Angular velocity must be finite.");
            return;
        }
        m_angularVelocity = velocity;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::addForce(const Vector3& force) noexcept
    {
        if (!PhysicsSystem::instance().canAdvancePhysics())
            return;

        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->addForce(*this, force, false))
                LOG_ERROR("[RigidbodyComponent] Failed to add force; the Rigidbody may not be active.");
        }
        else
        {
            LOG_ERROR("[RigidbodyComponent] Cannot add force because Physics is not initialized.");
        }
    }

    void RigidbodyComponent::addImpulse(const Vector3& impulse) noexcept
    {
        if (!PhysicsSystem::instance().canAdvancePhysics())
            return;

        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->addForce(*this, impulse, true))
                LOG_ERROR("[RigidbodyComponent] Failed to add impulse; the Rigidbody may not be active.");
        }
        else
        {
            LOG_ERROR("[RigidbodyComponent] Cannot add impulse because Physics is not initialized.");
        }
    }

    void RigidbodyComponent::addTorque(const Vector3& torque) noexcept
    {
        if (!PhysicsSystem::instance().canAdvancePhysics())
            return;

        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->addTorque(*this, torque))
                LOG_ERROR("[RigidbodyComponent] Failed to add torque; the Rigidbody may not be active.");
        }
        else
        {
            LOG_ERROR("[RigidbodyComponent] Cannot add torque because Physics is not initialized.");
        }
    }

    void RigidbodyComponent::setKinematic(const bool enabled) noexcept
    {
        if (m_isKinematic == enabled)
            return;
        m_isKinematic = enabled;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::setGravityEnabled(const bool enabled) noexcept
    {
        if (m_gravityEnabled == enabled)
            return;
        m_gravityEnabled = enabled;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::setLinearDamping(const float damping) noexcept
    {
        if (!std::isfinite(damping) || damping < 0.0f)
        {
            LOG_ERROR("[RigidbodyComponent] Linear damping must be a non-negative finite value.");
            return;
        }
        m_linearDamping = damping;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::setAngularDamping(const float damping) noexcept
    {
        if (!std::isfinite(damping) || damping < 0.0f)
        {
            LOG_ERROR("[RigidbodyComponent] Angular damping must be a non-negative finite value.");
            return;
        }
        m_angularDamping = damping;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateRigidbody(*this);
    }

    void RigidbodyComponent::wakeUp() noexcept
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->setSleeping(*this, false);
    }

    void RigidbodyComponent::sleep() noexcept
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->setSleeping(*this, true);
    }

    void RigidbodyComponent::onEnable()
    {
        PhysicsWorld* const world = PhysicsSystem::instance().getWorld();
        if (world == nullptr || !world->createRigidbody(*this))
            LOG_ERROR("[RigidbodyComponent] Failed to create the PhysX Rigidbody.");
    }

    void RigidbodyComponent::onDisable()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->destroyRigidbody(*this);
    }

    void RigidbodyComponent::onDestroy()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->forgetRigidbodyReferences(*this);
        onDisable();
    }

    void RigidbodyComponent::synchronizeTransform(
        const Vector3& position,
        const Quaternion& rotation) noexcept
    {
        GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return;

        TransformComponent* const transformComponent = gameObject->getComponent<TransformComponent>();
        if (transformComponent == nullptr)
        {
            LOG_ERROR("[RigidbodyComponent] GameObject has no TransformComponent.");
            return;
        }

        Vector3 localPosition = position;
        Quaternion localRotation = rotation;
        if (const GameObject* const parent = gameObject->getParent())
        {
            const Transform& parentTransform = parent->getWorldTransform();
            const Matrix inverseParent = parentTransform.toInverseMatrix();
            localPosition = Vector3::Transform(position, inverseParent);
            Quaternion inverseParentRotation;
            parentTransform.getRotation().Inverse(inverseParentRotation);
            localRotation = Quaternion::Concatenate(rotation, inverseParentRotation);
        }

        Transform& localTransform = transformComponent->localTransform();
        localTransform.setPosition(localPosition);
        localTransform.setRotation(localRotation);
    }
} // namespace Engine
