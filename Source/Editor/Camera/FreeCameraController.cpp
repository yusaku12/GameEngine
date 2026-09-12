#include "Pch.h"
#include "Editor\Camera\FreeCameraController.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\Input\InputManager.h"

namespace Engine
{
    namespace
    {
        constexpr float MIN_SETTING_VALUE = 0.0001f;
        constexpr float MAX_PITCH = DirectX::XM_PIDIV2 - 0.01f;
        constexpr float FAST_SPEED_MULTIPLIER = 4.0f;
        constexpr float SLOW_SPEED_MULTIPLIER = 0.25f;
    }

    FreeCameraController::FreeCameraController(CameraComponent& camera) noexcept
        : m_camera(&camera)
    {
    }

    void FreeCameraController::update(const float deltaTime) noexcept
    {
        if (!m_enabled || !std::isfinite(deltaTime) || deltaTime <= 0.0f
            || m_camera == nullptr || m_camera->getTransform() == nullptr)
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
            if (input.isKeyHeld('W')) movement += m_camera->getForward();
            if (input.isKeyHeld('S')) movement -= m_camera->getForward();
            if (input.isKeyHeld('D')) movement += m_camera->getRight();
            if (input.isKeyHeld('A')) movement -= m_camera->getRight();
            if (input.isKeyHeld('E')) movement += Vector3::UnitY;
            if (input.isKeyHeld('Q')) movement -= Vector3::UnitY;
            if (movement.LengthSquared() > 0.0f)
            {
                movement.Normalize();
                m_camera->getTransform()->translate(movement * speed * deltaTime);
            }
        }

        if (input.isMouseHeld(2))
        {
            const Vector3 pan = m_camera->getRight() * -static_cast<float>(mouseDelta.x)
                + m_camera->getUp() * static_cast<float>(mouseDelta.y);
            m_camera->getTransform()->translate(pan * m_panSensitivity * m_orbitDistance);
            m_orbitPoint += pan * m_panSensitivity * m_orbitDistance;
        }

        const int wheel = input.getMouseWheel();
        if (wheel != 0)
        {
            const float distance = static_cast<float>(wheel) * m_zoomSensitivity;
            m_camera->getTransform()->translate(m_camera->getForward() * distance);
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
        if (m_camera == nullptr || m_camera->getTransform() == nullptr)
            return;

        m_orbitPoint = Vector3(bounds.Center);
        const Vector3 extents(bounds.Extents);
        const float radius = std::max({ extents.x, extents.y, extents.z, MIN_SETTING_VALUE });
        const float halfFov = DirectX::XMConvertToRadians(m_camera->getFieldOfView()) * 0.5f;
        m_orbitDistance = m_camera->getProjectionMode() == CameraProjectionMode::Perspective
            ? radius / std::tan(halfFov) + radius
            : radius * 2.0f;
        m_camera->getTransform()->setPosition(m_orbitPoint - m_camera->getForward() * m_orbitDistance);
    }

    void FreeCameraController::setOrbitPoint(const Vector3& point) noexcept
    {
        m_orbitPoint = point;
        if (m_camera != nullptr)
            m_orbitDistance = std::max((m_camera->getPosition() - point).Length(), MIN_SETTING_VALUE);
    }

    void FreeCameraController::updateRotation(const float yawDelta, const float pitchDelta) noexcept
    {
        Transform* const transform = m_camera->getTransform();
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
        updateRotation(yawDelta, pitchDelta);
        m_camera->getTransform()->setPosition(m_orbitPoint - m_camera->getForward() * m_orbitDistance);
    }
} // namespace Engine