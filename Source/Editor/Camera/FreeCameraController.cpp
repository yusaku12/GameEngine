#include "Pch.h"
#include "Editor\Camera\FreeCameraController.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Input\InputManager.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        constexpr float MIN_SETTING_VALUE = 0.0001f;
        constexpr float MAX_PITCH = DirectX::XM_PIDIV2 - 0.01f;
        constexpr float FAST_SPEED_MULTIPLIER = 4.0f;
        constexpr float SLOW_SPEED_MULTIPLIER = 0.25f;

        const ComponentTypeID freeCameraControllerType =
            ComponentRegistry::instance().registerType<FreeCameraController>(
                "Free Camera Controller", false, false, true);
    }

    FreeCameraController::FreeCameraController() noexcept
    {
        GE_UNUSED(freeCameraControllerType);
    }

    void FreeCameraController::onUpdate(const float deltaTime)
    {
        CameraComponent* const camera = getCamera();
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f
            || camera == nullptr || getTransform() == nullptr)
        {
            return;
        }

        InputManager& input = InputManager::instance();
        const POINT mouseDelta = input.getMouseDelta();
        const bool orbiting = input.isKeyHeld(VK_MENU) && input.isMouseHeld(0);
        if (orbiting)
        {
            updateOrbit(
                static_cast<float>(mouseDelta.x) * m_lookSensitivity,
                static_cast<float>(mouseDelta.y) * m_lookSensitivity);
        }
        else if (input.isMouseHeld(1))
        {
            updateRotation(
                static_cast<float>(mouseDelta.x) * m_lookSensitivity,
                static_cast<float>(mouseDelta.y) * m_lookSensitivity);

            float speed = m_moveSpeed;
            if (input.isKeyHeld(VK_SHIFT))
                speed *= FAST_SPEED_MULTIPLIER;
            if (input.isKeyHeld(VK_CONTROL))
                speed *= SLOW_SPEED_MULTIPLIER;

            Vector3 movement = Vector3::Zero;
            if (input.isKeyHeld('W')) movement += camera->getForward();
            if (input.isKeyHeld('S')) movement -= camera->getForward();
            if (input.isKeyHeld('D')) movement += camera->getRight();
            if (input.isKeyHeld('A')) movement -= camera->getRight();
            if (input.isKeyHeld('E')) movement += Vector3::UnitY;
            if (input.isKeyHeld('Q')) movement -= Vector3::UnitY;
            if (movement.LengthSquared() > 0.0f)
            {
                movement.Normalize();
                getTransform()->translate(movement * speed * deltaTime);
            }
        }

        if (input.isMouseHeld(2))
        {
            const Vector3 pan = camera->getRight() * -static_cast<float>(mouseDelta.x)
                + camera->getUp() * static_cast<float>(mouseDelta.y);
            getTransform()->translate(pan * m_panSensitivity * m_orbitDistance);
            m_orbitPoint += pan * m_panSensitivity * m_orbitDistance;
        }

        const int wheel = input.getMouseWheel();
        if (wheel != 0)
        {
            const float distance = static_cast<float>(wheel) * m_zoomSensitivity;
            getTransform()->translate(camera->getForward() * distance);
            m_orbitDistance = std::max(MIN_SETTING_VALUE, m_orbitDistance - distance);
        }
    }

    void FreeCameraController::setMoveSpeed(const float speed) noexcept
    {
        if (std::isfinite(speed) && speed > MIN_SETTING_VALUE)
            m_moveSpeed = speed;
    }

    void FreeCameraController::setLookSensitivity(const float sensitivity) noexcept
    {
        if (std::isfinite(sensitivity) && sensitivity > MIN_SETTING_VALUE)
            m_lookSensitivity = sensitivity;
    }

    void FreeCameraController::focus(const AABB& bounds) noexcept
    {
        CameraComponent* const camera = getCamera();
        if (camera == nullptr || getTransform() == nullptr)
            return;

        m_orbitPoint = Vector3(bounds.Center);
        const Vector3 extents(bounds.Extents);
        const float radius = std::max({ extents.x, extents.y, extents.z, MIN_SETTING_VALUE });
        const float halfFov = DirectX::XMConvertToRadians(camera->getFieldOfView()) * 0.5f;
        m_orbitDistance = camera->getProjectionMode() == CameraProjectionMode::Perspective
            ? radius / std::tan(halfFov) + radius
            : radius * 2.0f;
        getTransform()->setPosition(m_orbitPoint - camera->getForward() * m_orbitDistance);
    }

    void FreeCameraController::setOrbitPoint(const Vector3& point) noexcept
    {
        m_orbitPoint = point;
        if (CameraComponent* const camera = getCamera())
            m_orbitDistance = std::max((camera->getPosition() - point).Length(), MIN_SETTING_VALUE);
    }

    CameraComponent* FreeCameraController::getCamera() noexcept
    {
        GameObject* const gameObject = getGameObject();
        return gameObject != nullptr ? gameObject->getComponent<CameraComponent>() : nullptr;
    }

    void FreeCameraController::updateRotation(const float yawDelta, const float pitchDelta) noexcept
    {
        Transform* const transform = getTransform();
        if (!m_rotationInitialized)
        {
            const Vector3 rotation = transform->getEulerAngles();
            m_pitch = rotation.x;
            m_yaw = rotation.y;
            m_rotationInitialized = true;
        }
        m_yaw += yawDelta;
        m_pitch = std::clamp(m_pitch + pitchDelta, -MAX_PITCH, MAX_PITCH);
        transform->setEulerAngles(m_pitch, m_yaw, 0.0f);
    }

    void FreeCameraController::updateOrbit(const float yawDelta, const float pitchDelta) noexcept
    {
        CameraComponent* const camera = getCamera();
        if (camera == nullptr || getTransform() == nullptr)
            return;
        updateRotation(yawDelta, pitchDelta);
        getTransform()->setPosition(m_orbitPoint - camera->getForward() * m_orbitDistance);
    }

    void FreeCameraController::onImGui()
    {
        float moveSpeed = m_moveSpeed;
        if (ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, MIN_SETTING_VALUE, 1000.0f))
            setMoveSpeed(moveSpeed);

        float lookSensitivity = m_lookSensitivity;
        if (ImGui::DragFloat("Look Sensitivity", &lookSensitivity, 0.0001f,
            MIN_SETTING_VALUE, 1.0f, "%.4f"))
        {
            setLookSensitivity(lookSensitivity);
        }
    }
} // namespace Engine