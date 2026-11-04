#include "Pch.h"
#include "Components\Physics\Character\CharacterControllerMovementComponent.h"
#include "Components\Rendering\CameraComponent.h"
#include "Components\Transform\TransformComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Input\InputManager.h"
#include "Graphics\Camera\CameraManager.h"
#include "Components\Physics\Character\CharacterControllerComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\PhysicsQuery.h"
#include "Components\Physics\Rigidbody\RigidbodyComponent.h"

namespace Engine
{
    namespace
    {
        constexpr float CAMERA_DISTANCE = 5.0f;
        constexpr float CAMERA_COLLISION_RADIUS = 0.2f;
        constexpr float CAMERA_COLLISION_MARGIN = 0.1f;
        constexpr float MIN_CAMERA_DISTANCE = 0.1f;
        constexpr float CAMERA_PITCH_MIN = -0.75f;
        constexpr float CAMERA_PITCH_MAX = 0.25f;
        constexpr float CAMERA_MOUSE_SENSITIVITY = 0.005f;
        constexpr float MAX_TURN_RATE = DirectX::XM_2PI;
        constexpr float MOVEMENT_SPEED = 5.0f;
        constexpr float GRAVITY = 9.81f;
        constexpr float GROUNDED_VELOCITY = -2.0f;

        bool isFinite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFinite(const Quaternion& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool isChildOf(const GameObject& candidate, const GameObject& ancestor) noexcept
        {
            for (const GameObject* parent = candidate.getParent(); parent != nullptr; parent = parent->getParent())
                if (parent == &ancestor)
                    return true;
            return false;
        }
    }

    void CharacterControllerMovementComponent::onEnable()
    {
        m_verticalVelocity = 0.0f;
        m_reportedInvalidSetup = false;
        m_reportedInvalidCamera = false;
    }

    void CharacterControllerMovementComponent::onDisable()
    {
        m_verticalVelocity = 0.0f;
    }

    void CharacterControllerMovementComponent::onUpdate(const float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            LOG_ERROR("[CharacterControllerMovement] Frame delta time must be finite and nonnegative.");
            return;
        }

        InputManager& input = InputManager::instance();
        if (!input.isMouseHeld(1))
            return;

        const POINT mouseDelta = input.getMouseDelta();
        m_cameraYawOffset = std::remainder(
            m_cameraYawOffset + static_cast<float>(mouseDelta.x) * CAMERA_MOUSE_SENSITIVITY,
            DirectX::XM_2PI);
        m_cameraPitch = std::clamp(
            m_cameraPitch - static_cast<float>(mouseDelta.y) * CAMERA_MOUSE_SENSITIVITY,
            CAMERA_PITCH_MIN,
            CAMERA_PITCH_MAX);
    }

    void CharacterControllerMovementComponent::onFixedUpdate(const float fixedDeltaTime)
    {
        if (!std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
        {
            LOG_ERROR("[CharacterControllerMovement] Fixed delta time must be finite and positive.");
            return;
        }

        GameObject* const gameObject = getGameObject();
        CharacterControllerComponent* const controller =
            gameObject != nullptr ? gameObject->getComponent<CharacterControllerComponent>() : nullptr;
        if (gameObject == nullptr || controller == nullptr || !controller->isEnabled()
            || gameObject->hasComponent<RigidbodyComponent>())
        {
            if (!m_reportedInvalidSetup)
            {
                LOG_ERROR("[CharacterControllerMovement] Requires an enabled CharacterController and no Rigidbody on the same GameObject.");
                m_reportedInvalidSetup = true;
            }
            return;
        }
        m_reportedInvalidSetup = false;

        InputManager& input = InputManager::instance();
        Vector3 movement = Vector3::Zero;
        if (input.isKeyHeld('W')) movement.z += 1.0f;
        if (input.isKeyHeld('S')) movement.z -= 1.0f;
        if (input.isKeyHeld('D')) movement.x += 1.0f;
        if (input.isKeyHeld('A')) movement.x -= 1.0f;
        const float movementLengthSquared = movement.LengthSquared();
        if (!std::isfinite(movementLengthSquared))
        {
            LOG_ERROR("[CharacterControllerMovement] Movement direction is not finite.");
            return;
        }
        if (movementLengthSquared > 1.0f)
            movement /= std::sqrt(movementLengthSquared);

        if (movementLengthSquared > 0.0f)
        {
            const float targetYaw = std::atan2(movement.x, movement.z);
            Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, gameObject->getWorldRotation());
            currentForward.y = 0.0f;
            const float forwardLengthSquared = currentForward.LengthSquared();
            if (!isFinite(currentForward) || !std::isfinite(forwardLengthSquared) || forwardLengthSquared <= 0.0f)
            {
                LOG_ERROR("[CharacterControllerMovement] Cannot determine the Player's horizontal facing direction.");
                return;
            }
            currentForward /= std::sqrt(forwardLengthSquared);
            const float currentYaw = std::atan2(currentForward.x, currentForward.z);
            const float yawDelta = std::remainder(targetYaw - currentYaw, DirectX::XM_2PI);
            const float yawStep = std::clamp(yawDelta,
                -MAX_TURN_RATE * fixedDeltaTime, MAX_TURN_RATE * fixedDeltaTime);
            const Quaternion worldRotation =
                Quaternion::CreateFromYawPitchRoll(currentYaw + yawStep, 0.0f, 0.0f);

            Quaternion localRotation = worldRotation;
            if (const GameObject* const parent = gameObject->getParent())
            {
                const Quaternion parentRotation = parent->getWorldRotation();
                Quaternion inverseParentRotation;
                const float parentRotationLengthSquared = parentRotation.LengthSquared();
                if (!isFinite(parentRotation) || !std::isfinite(parentRotationLengthSquared)
                    || parentRotationLengthSquared <= 0.0f)
                {
                    LOG_ERROR("[CharacterControllerMovement] Cannot rotate the Player under an invalid parent transform.");
                    return;
                }
                parentRotation.Inverse(inverseParentRotation);
                if (!isFinite(inverseParentRotation))
                {
                    LOG_ERROR("[CharacterControllerMovement] Cannot rotate the Player under an invalid parent transform.");
                    return;
                }
                localRotation = Quaternion::Concatenate(worldRotation, inverseParentRotation);
            }
            if (!isFinite(localRotation))
            {
                LOG_ERROR("[CharacterControllerMovement] Player rotation is not finite.");
                return;
            }
            gameObject->getTransform()->setRotation(localRotation);
        }

        if (controller->isGrounded() && m_verticalVelocity < 0.0f)
            m_verticalVelocity = GROUNDED_VELOCITY;
        else
            m_verticalVelocity -= GRAVITY * fixedDeltaTime;

        movement *= MOVEMENT_SPEED * fixedDeltaTime;
        movement.y = m_verticalVelocity * fixedDeltaTime;
        controller->move(movement, fixedDeltaTime);
    }

    void CharacterControllerMovementComponent::onLateUpdate([[maybe_unused]] const float deltaTime)
    {
        GameObject* const player = getGameObject();
        CameraComponent* const camera = CameraManager::instance().getActiveCamera();
        GameObject* const cameraObject = camera != nullptr ? camera->getGameObject() : nullptr;
        TransformComponent* const cameraTransform =
            cameraObject != nullptr ? cameraObject->getComponent<TransformComponent>() : nullptr;
        CharacterControllerComponent* const controller =
            player != nullptr ? player->getComponent<CharacterControllerComponent>() : nullptr;
        if (player == nullptr || camera == nullptr || cameraObject == nullptr
            || cameraTransform == nullptr || controller == nullptr || !controller->isEnabled()
            || !isChildOf(*cameraObject, *player))
        {
            if (!m_reportedInvalidCamera)
            {
                LOG_WARNING("[CharacterControllerMovement] Add the active Main Camera below the Player in the GameObject hierarchy.");
                m_reportedInvalidCamera = true;
            }
            return;
        }
        m_reportedInvalidCamera = false;

        const Quaternion playerRotation = player->getWorldRotation();
        Vector3 playerForward = Vector3::Transform(Vector3::UnitZ, playerRotation);
        playerForward.y = 0.0f;
        const float forwardLengthSquared = playerForward.LengthSquared();
        if (!isFinite(playerForward) || !std::isfinite(forwardLengthSquared) || forwardLengthSquared <= 0.0f)
        {
            LOG_ERROR("[CharacterControllerMovement] Cannot position the TPS camera for an invalid Player rotation.");
            return;
        }
        playerForward /= std::sqrt(forwardLengthSquared);
        const float playerYaw = std::atan2(playerForward.x, playerForward.z);
        const float cameraYaw = playerYaw + m_cameraYawOffset;
        const float cosPitch = std::cos(m_cameraPitch);
        const Vector3 cameraForward(
            std::sin(cameraYaw) * cosPitch,
            std::sin(m_cameraPitch),
            std::cos(cameraYaw) * cosPitch);
        const Vector3 focus = player->getWorldPosition()
            + Vector3::UnitY * (controller->getHeight() * 0.75f);
        const Vector3 cameraOffset = -cameraForward * CAMERA_DISTANCE;
        float cameraDistance = CAMERA_DISTANCE;
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            PhysicsQueryFilter filter;
            filter.ignoredGameObject = player;
            if (const auto hit = world->sphereCast(
                focus, CAMERA_COLLISION_RADIUS, cameraOffset, CAMERA_DISTANCE, filter))
            {
                cameraDistance = std::clamp(
                    hit->distance - CAMERA_COLLISION_MARGIN,
                    MIN_CAMERA_DISTANCE,
                    CAMERA_DISTANCE);
            }
        }
        const Vector3 cameraPosition = focus + cameraOffset * (cameraDistance / CAMERA_DISTANCE);
        const Quaternion cameraRotation =
            Quaternion::CreateFromYawPitchRoll(cameraYaw, m_cameraPitch, 0.0f);
        if (!isFinite(focus) || !isFinite(cameraPosition) || !isFinite(cameraRotation))
        {
            LOG_ERROR("[CharacterControllerMovement] TPS camera transform is not finite.");
            return;
        }

        Transform localCameraTransform;
        if (const GameObject* const parent = cameraObject->getParent())
        {
            const Matrix parentWorld = parent->getWorldMatrix();
            const float determinant = parentWorld.Determinant();
            if (!std::isfinite(determinant) || determinant == 0.0f)
            {
                LOG_ERROR("[CharacterControllerMovement] Cannot follow the Player through a singular camera-parent transform.");
                return;
            }
            const Transform worldCameraTransform(cameraPosition, cameraRotation, cameraObject->getWorldScale());
            if (!Transform::tryFromMatrix(
                worldCameraTransform.toMatrix() * parentWorld.Invert(), localCameraTransform))
            {
                LOG_ERROR("[CharacterControllerMovement] Camera-parent transform cannot represent the TPS camera pose as TRS.");
                return;
            }
        }
        else
        {
            localCameraTransform = Transform(
                cameraPosition, cameraRotation, cameraTransform->localTransform().getScale());
        }

        cameraTransform->localTransform().setPosition(localCameraTransform.getPosition());
        cameraTransform->localTransform().setRotation(localCameraTransform.getRotation());
        cameraTransform->localTransform().setScale(localCameraTransform.getScale());
    }
} // namespace Engine
