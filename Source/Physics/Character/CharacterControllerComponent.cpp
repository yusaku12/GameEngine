#include "Pch.h"
#include "Core\GameObject\GameObject.h"
#include "Physics\Character\CharacterControllerComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PhysicsComponent_generated.h"
#include "Physics\Internal\PhysicsPayload.h"
#include <imgui.h>
#include <limits>

namespace Engine
{
    namespace
    {
        bool isFiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    void CharacterControllerComponent::move(
        const Vector3& displacement,
        const float deltaTime) noexcept
    {
        if (!PhysicsSystem::instance().canAdvancePhysics())
            return;

        if (!isFiniteVector(displacement) || !std::isfinite(deltaTime) || deltaTime <= 0.0f)
        {
            LOG_ERROR("[CharacterControllerComponent] Movement and delta time must be finite; delta time must be positive.");
            return;
        }

        PhysicsWorld* const world = PhysicsSystem::instance().getWorld();
        if (world == nullptr || !world->moveController(*this, displacement, deltaTime))
            LOG_ERROR("[CharacterControllerComponent] Failed to move the Character Controller.");
    }

    void CharacterControllerComponent::setRadius(const float radius) noexcept
    {
        if (!std::isfinite(radius) || radius <= 0.0f || radius * 2.0f >= m_height)
        {
            LOG_ERROR("[CharacterControllerComponent] Radius must be positive, finite, and less than half the total height.");
            return;
        }
        if (m_radius == radius)
            return;

        m_radius = radius;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateController(*this);
    }

    void CharacterControllerComponent::setHeight(const float height) noexcept
    {
        if (!std::isfinite(height) || height <= m_radius * 2.0f)
        {
            LOG_ERROR("[CharacterControllerComponent] Total height must be finite and greater than the capsule diameter.");
            return;
        }
        if (m_height == height)
            return;

        m_height = height;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateController(*this);
    }

    void CharacterControllerComponent::setLayer(const PhysicsLayer layer) noexcept
    {
        if (physicsLayerBit(layer) == 0)
        {
            LOG_ERROR("[CharacterControllerComponent] Physics layer index must be in [0, 31].");
            return;
        }
        if (m_layer == layer)
            return;

        m_layer = layer;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateController(*this);
    }

    void CharacterControllerComponent::setCollisionMask(const PhysicsLayerMask mask) noexcept
    {
        if (m_collisionMask == mask)
            return;

        m_collisionMask = mask;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateController(*this);
    }

    std::uint32_t CharacterControllerComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_PHYSICS_COMPONENT_VERSION;
    }

    bool CharacterControllerComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder(64);
        const auto root = Serialization::CreateCharacterControllerComponentPayload(builder,
            m_radius, m_height, static_cast<std::uint32_t>(m_layer), m_collisionMask);
        PhysicsSerialization::writePayload(builder, root, "CCCP", payload);
        return true;
    }

    bool CharacterControllerComponent::deserializePayload(
        const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        if (version == 0 && payload.empty())
            return true;
        const Serialization::CharacterControllerComponentPayload* source = nullptr;
        if (version < Serialization::MINIMUM_SUPPORTED_PHYSICS_COMPONENT_VERSION
            || version > Serialization::CURRENT_PHYSICS_COMPONENT_VERSION
            || !PhysicsSerialization::readPayload(payload, "CCCP", source)
            || !std::isfinite(source->radius()) || !std::isfinite(source->height())
            || source->radius() <= 0.0f || source->height() <= source->radius() * 2.0f
            || source->layer() >= 32)
            return false;

        setRadius(std::min(m_radius, source->radius()));
        setHeight(source->height());
        setRadius(source->radius());
        setLayer(static_cast<PhysicsLayer>(source->layer()));
        setCollisionMask(source->collision_mask());
        return true;
    }

    void CharacterControllerComponent::onImGui()
    {
        float radius = getRadius();
        const float maximumRadius = std::nextafter(getHeight() * 0.5f, 0.0f);
        if (ImGui::DragFloat("Radius", &radius, 0.05f,
            std::min(0.01f, maximumRadius), maximumRadius, "%.3f", ImGuiSliderFlags_AlwaysClamp))
            setRadius(radius);

        float height = getHeight();
        const float minimumHeight = std::nextafter(getRadius() * 2.0f,
            std::numeric_limits<float>::infinity());
        if (ImGui::DragFloat("Height", &height, 0.05f,
            minimumHeight, std::numeric_limits<float>::max(), "%.3f", ImGuiSliderFlags_AlwaysClamp))
            setHeight(height);
        ImGui::TextDisabled("Total height must be greater than twice the radius.");

        constexpr const char* layerNames[] = {
            "Default", "Player", "Enemy", "Environment", "Projectile", "Trigger"
        };
        int layer = static_cast<int>(getLayer());
        if (ImGui::Combo("Layer", &layer, layerNames, static_cast<int>(std::size(layerNames))))
            setLayer(static_cast<PhysicsLayer>(layer));

        PhysicsLayerMask mask = getCollisionMask();
        if (ImGui::InputScalar("Collision Mask", ImGuiDataType_U32, &mask, nullptr, nullptr,
            "%08X", ImGuiInputTextFlags_CharsHexadecimal))
            setCollisionMask(mask);
        ImGui::Text("Grounded: %s", isGrounded() ? "Yes" : "No");
    }

    void CharacterControllerComponent::onPayloadRestored()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->createController(*this))
                LOG_ERROR("[CharacterControllerComponent] Failed to recreate the controller after payload restoration.");
        }
    }

    void CharacterControllerComponent::onEnable()
    {
        GameObject* const gameObject = getGameObject();
        PhysicsWorld* const world = PhysicsSystem::instance().getWorld();
        if (gameObject == nullptr || world == nullptr || !world->createController(*this))
            LOG_ERROR("[CharacterControllerComponent] Failed to create the PhysX Character Controller.");
    }

    void CharacterControllerComponent::onDisable()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->destroyController(*this);
        m_isGrounded = false;
    }

    void CharacterControllerComponent::onDestroy()
    {
        onDisable();
    }
} // namespace Engine
