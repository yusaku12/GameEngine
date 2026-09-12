#include "Pch.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Graphics\Camera\CameraManager.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Camera\CameraUtils.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        constexpr float MIN_FIELD_OF_VIEW = 1.0f;
        constexpr float MAX_FIELD_OF_VIEW = 179.0f;
        constexpr float MIN_POSITIVE_VALUE = 0.0001f;

        const ComponentTypeID cameraType = ComponentRegistry::instance().registerType<CameraComponent>(
            "Camera", false, false, true);

        bool isFinite(const float value) noexcept
        {
            return std::isfinite(value);
        }

        bool isFinite(const Vector2& value) noexcept
        {
            return isFinite(value.x) && isFinite(value.y);
        }

        bool isFinite(const Color& value) noexcept
        {
            return isFinite(value.x) && isFinite(value.y) && isFinite(value.z) && isFinite(value.w);
        }
    }

    CameraComponent::CameraComponent() noexcept
    {
        GE_UNUSED(cameraType);
    }

    void CameraComponent::setProjectionMode(const CameraProjectionMode mode) noexcept
    {
        if (m_projectionMode == mode)
            return;
        m_projectionMode = mode;
        invalidateProjection();
    }

    void CameraComponent::setFieldOfView(const float fieldOfViewDegrees) noexcept
    {
        if (!isFinite(fieldOfViewDegrees))
        {
            LOG_WARNING("[Camera] FOVに有限値ではない値が指定されました");
            return;
        }

        const float clamped = std::clamp(fieldOfViewDegrees, MIN_FIELD_OF_VIEW, MAX_FIELD_OF_VIEW);
        if (m_fieldOfViewDegrees == clamped)
            return;
        m_fieldOfViewDegrees = clamped;
        invalidateProjection();
    }

    void CameraComponent::setNearClipPlane(const float nearClip) noexcept
    {
        if (!isFinite(nearClip) || nearClip <= 0.0f || nearClip >= m_farClip)
        {
            LOG_WARNING("[Camera] Near Clipは0より大きくFar Clip未満である必要があります");
            return;
        }
        if (m_nearClip == nearClip)
            return;
        m_nearClip = nearClip;
        invalidateProjection();
    }

    void CameraComponent::setFarClipPlane(const float farClip) noexcept
    {
        if (!isFinite(farClip) || farClip <= m_nearClip)
        {
            LOG_WARNING("[Camera] Far ClipはNear Clipより大きい必要があります");
            return;
        }
        if (m_farClip == farClip)
            return;
        m_farClip = farClip;
        invalidateProjection();
    }

    void CameraComponent::setAspectRatio(const float aspectRatio) noexcept
    {
        if (!isFinite(aspectRatio) || aspectRatio <= MIN_POSITIVE_VALUE)
        {
            LOG_WARNING("[Camera] Aspect Ratioは0より大きい有限値である必要があります");
            return;
        }
        if (m_aspectRatio == aspectRatio)
            return;
        m_aspectRatio = aspectRatio;
        invalidateProjection();
    }

    void CameraComponent::setOrthographicSize(const float size) noexcept
    {
        if (!isFinite(size) || size <= MIN_POSITIVE_VALUE)
        {
            LOG_WARNING("[Camera] Orthographic Sizeは0より大きい有限値である必要があります");
            return;
        }
        if (m_orthographicSize == size)
            return;
        m_orthographicSize = size;
        invalidateProjection();
    }

    void CameraComponent::setViewport(const CameraViewport& viewport) noexcept
    {
        if (!isFinite(viewport.x) || !isFinite(viewport.y)
            || !isFinite(viewport.width) || !isFinite(viewport.height)
            || viewport.width <= 0.0f || viewport.height <= 0.0f)
        {
            LOG_WARNING("[Camera] Viewportに不正な値が指定されました");
            return;
        }

        CameraViewport clamped{};
        clamped.x = std::clamp(viewport.x, 0.0f, 1.0f - MIN_POSITIVE_VALUE);
        clamped.y = std::clamp(viewport.y, 0.0f, 1.0f - MIN_POSITIVE_VALUE);
        clamped.width = std::clamp(viewport.width, MIN_POSITIVE_VALUE, 1.0f - clamped.x);
        clamped.height = std::clamp(viewport.height, MIN_POSITIVE_VALUE, 1.0f - clamped.y);
        if (std::memcmp(&m_viewport, &clamped, sizeof(CameraViewport)) == 0)
            return;

        m_viewport = clamped;
        setAspectRatio((m_renderTargetSize.x * m_viewport.width)
            / (m_renderTargetSize.y * m_viewport.height));
    }

    void CameraComponent::setRenderTargetSize(const std::uint32_t width, const std::uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
            return;
        m_renderTargetSize = Vector2(static_cast<float>(width), static_cast<float>(height));
        setAspectRatio((m_renderTargetSize.x * m_viewport.width)
            / (m_renderTargetSize.y * m_viewport.height));
    }

    const Matrix& CameraComponent::getViewMatrix() const noexcept
    {
        synchronizeView();
        return m_view;
    }

    const Matrix& CameraComponent::getProjectionMatrix() const noexcept
    {
        synchronizeProjection();
        return m_projection;
    }

    const Matrix& CameraComponent::getViewProjectionMatrix() const noexcept
    {
        synchronizeViewProjection();
        return m_viewProjection;
    }

    const Matrix& CameraComponent::getInverseViewMatrix() const noexcept
    {
        synchronizeView();
        return m_inverseView;
    }

    const Matrix& CameraComponent::getInverseProjectionMatrix() const noexcept
    {
        synchronizeProjection();
        return m_inverseProjection;
    }

    const Matrix& CameraComponent::getInverseViewProjectionMatrix() const noexcept
    {
        synchronizeViewProjection();
        return m_inverseViewProjection;
    }

    const Matrix& CameraComponent::getPreviousViewProjectionMatrix() const noexcept
    {
        synchronizeViewProjection();
        return m_hasPreviousViewProjection ? m_previousViewProjection : m_viewProjection;
    }

    Vector3 CameraComponent::getPosition() const noexcept
    {
        const GameObject* const gameObject = getGameObject();
        return gameObject != nullptr ? gameObject->getWorldPosition() : Vector3::Zero;
    }

    Vector3 CameraComponent::getForward() const noexcept
    {
        const GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return Vector3::UnitZ;
        Vector3 direction = Vector3::Transform(Vector3::UnitZ, gameObject->getWorldRotation());
        direction.Normalize();
        return direction;
    }

    Vector3 CameraComponent::getRight() const noexcept
    {
        const GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return Vector3::UnitX;
        Vector3 direction = Vector3::Transform(Vector3::UnitX, gameObject->getWorldRotation());
        direction.Normalize();
        return direction;
    }

    Vector3 CameraComponent::getUp() const noexcept
    {
        const GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return Vector3::UnitY;
        Vector3 direction = Vector3::Transform(Vector3::UnitY, gameObject->getWorldRotation());
        direction.Normalize();
        return direction;
    }

    const Frustum& CameraComponent::getFrustum() const noexcept
    {
        synchronizeViewProjection();
        if (m_frustumDirty)
        {
            m_frustum = CameraUtils::buildWorldFrustum(m_projection, m_inverseView);
            m_frustumDirty = false;
        }
        return m_frustum;
    }

    Vector3 CameraComponent::screenToWorldPoint(const Vector3& screenPoint) const noexcept
    {
        const Viewport viewport = getPixelViewport();
        return viewport.Unproject(screenPoint, getProjectionMatrix(), getViewMatrix(), Matrix::Identity);
    }

    Vector3 CameraComponent::worldToScreenPoint(const Vector3& worldPoint) const noexcept
    {
        const Viewport viewport = getPixelViewport();
        return viewport.Project(worldPoint, getProjectionMatrix(), getViewMatrix(), Matrix::Identity);
    }

    Ray CameraComponent::screenPointToRay(const Vector2& screenPoint) const noexcept
    {
        const Vector3 nearPoint = screenToWorldPoint(Vector3(screenPoint.x, screenPoint.y, 0.0f));
        const Vector3 farPoint = screenToWorldPoint(Vector3(screenPoint.x, screenPoint.y, 1.0f));
        const Vector3 origin = m_projectionMode == CameraProjectionMode::Perspective ? getPosition() : nearPoint;
        Vector3 direction = m_projectionMode == CameraProjectionMode::Perspective
            ? farPoint - origin : getForward();
        direction.Normalize();
        return Ray(origin, direction);
    }

    void CameraComponent::setProjectionJitter(const Vector2& jitter) noexcept
    {
        if (!isFinite(jitter))
        {
            LOG_WARNING("[Camera] Projection Jitterに有限値ではない値が指定されました");
            return;
        }
        if (m_projectionJitter == jitter)
            return;
        m_projectionJitter = jitter;
        invalidateProjection();
    }

    void CameraComponent::setBackgroundColor(const Color& color) noexcept
    {
        if (!isFinite(color))
        {
            LOG_WARNING("[Camera] Background Colorに有限値ではない値が指定されました");
            return;
        }
        m_backgroundColor = Color(
            std::clamp(color.x, 0.0f, 1.0f),
            std::clamp(color.y, 0.0f, 1.0f),
            std::clamp(color.z, 0.0f, 1.0f),
            std::clamp(color.w, 0.0f, 1.0f));
    }

    RenderView CameraComponent::buildRenderView() const noexcept
    {
        synchronizeViewProjection();
        const Vector2 viewportSize(
            m_renderTargetSize.x * m_viewport.width,
            m_renderTargetSize.y * m_viewport.height);

        RenderView result{};
        result.camera.view = m_view;
        result.camera.projection = m_projection;
        result.camera.viewProjection = m_viewProjection;
        result.camera.inverseView = m_inverseView;
        result.camera.inverseProjection = m_inverseProjection;
        result.camera.inverseViewProjection = m_inverseViewProjection;
        result.camera.previousViewProjection = getPreviousViewProjectionMatrix();
        result.camera.position = getPosition();
        result.camera.nearClip = m_nearClip;
        result.camera.forward = getForward();
        result.camera.farClip = m_farClip;
        result.camera.viewportSize = viewportSize;
        result.camera.inverseViewportSize = Vector2(1.0f / viewportSize.x, 1.0f / viewportSize.y);
        result.camera.jitter = m_projectionJitter;
        result.frustum = getFrustum();
        result.viewport = m_viewport;
        result.clearMode = m_clearMode;
        result.backgroundColor = m_backgroundColor;
        result.cullingMask = m_cullingMask;
        result.priority = m_priority;
        return result;
    }

    void CameraComponent::onAwake()
    {
        CameraManager::instance().registerCamera(this);
    }

    void CameraComponent::onLateUpdate([[maybe_unused]] const float deltaTime)
    {
        if (CameraManager::instance().getActiveCamera() != this)
            return;

        const RenderView view = buildRenderView();
        CameraRenderSubmissionQueue::instance().submit(view);
        m_previousViewProjection = view.camera.viewProjection;
        m_hasPreviousViewProjection = true;
    }

    void CameraComponent::onDestroy()
    {
        CameraManager::instance().unregisterCamera(this);
    }

    void CameraComponent::onImGui()
    {
        int projectionMode = static_cast<int>(m_projectionMode);
        constexpr const char* projectionModes[] = { "Perspective", "Orthographic" };
        if (ImGui::Combo("Projection", &projectionMode, projectionModes, std::size(projectionModes)))
            setProjectionMode(static_cast<CameraProjectionMode>(projectionMode));

        if (m_projectionMode == CameraProjectionMode::Perspective)
        {
            float fieldOfView = m_fieldOfViewDegrees;
            if (ImGui::SliderFloat("Field Of View", &fieldOfView, MIN_FIELD_OF_VIEW, MAX_FIELD_OF_VIEW, "%.1f deg"))
                setFieldOfView(fieldOfView);
        }
        else
        {
            float orthographicSize = m_orthographicSize;
            if (ImGui::DragFloat("Orthographic Size", &orthographicSize, 0.1f, MIN_POSITIVE_VALUE, 100000.0f))
                setOrthographicSize(orthographicSize);
        }

        float nearClip = m_nearClip;
        if (ImGui::DragFloat("Near Clip Plane", &nearClip, 0.01f, MIN_POSITIVE_VALUE, m_farClip - MIN_POSITIVE_VALUE))
            setNearClipPlane(nearClip);
        float farClip = m_farClip;
        if (ImGui::DragFloat("Far Clip Plane", &farClip, 1.0f, m_nearClip + MIN_POSITIVE_VALUE, 1000000.0f))
            setFarClipPlane(farClip);
        float aspectRatio = m_aspectRatio;
        if (ImGui::DragFloat("Aspect Ratio", &aspectRatio, 0.01f, MIN_POSITIVE_VALUE, 100.0f))
            setAspectRatio(aspectRatio);

        float viewport[] = { m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height };
        if (ImGui::DragFloat4("Viewport", viewport, 0.01f, 0.0f, 1.0f))
            setViewport(CameraViewport{ viewport[0], viewport[1], viewport[2], viewport[3] });

        ImGui::InputInt("Priority", &m_priority);
        int clearMode = static_cast<int>(m_clearMode);
        constexpr const char* clearModes[] = { "Skybox", "Solid Color", "Depth Only", "Nothing" };
        if (ImGui::Combo("Clear Mode", &clearMode, clearModes, std::size(clearModes)))
            setClearMode(static_cast<CameraClearMode>(clearMode));
        if (m_clearMode == CameraClearMode::SolidColor)
        {
            float backgroundColor[] = {
                m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z, m_backgroundColor.w
            };
            if (ImGui::ColorEdit4("Background", backgroundColor))
                setBackgroundColor(Color(backgroundColor));
        }
        ImGui::InputScalar("Culling Mask", ImGuiDataType_U32, &m_cullingMask, nullptr, nullptr, "%08X",
            ImGuiInputTextFlags_CharsHexadecimal);
        bool isMainCamera = CameraManager::instance().getMainCamera() == this;
        if (ImGui::Checkbox("Main Camera", &isMainCamera))
            CameraManager::instance().setMainCamera(isMainCamera ? this : nullptr);
    }

    void CameraComponent::invalidateProjection() noexcept
    {
        m_projectionDirty = true;
        m_viewProjectionDirty = true;
        m_frustumDirty = true;
    }

    void CameraComponent::synchronizeView() const noexcept
    {
        const GameObject* const gameObject = getGameObject();
        const std::uint64_t revision = gameObject != nullptr
            ? gameObject->getWorldTransformRevision() : 0;
        if (!m_viewDirty && revision == m_transformRevision)
            return;

        m_inverseView = gameObject != nullptr ? gameObject->getWorldMatrix() : Matrix::Identity;
        m_view = m_inverseView.Invert();
        m_transformRevision = revision;
        m_viewDirty = false;
        m_viewProjectionDirty = true;
        m_frustumDirty = true;
    }

    void CameraComponent::synchronizeProjection() const noexcept
    {
        if (!m_projectionDirty)
            return;

        m_projection = m_projectionMode == CameraProjectionMode::Perspective
            ? CameraUtils::buildPerspective(m_fieldOfViewDegrees, m_aspectRatio, m_nearClip, m_farClip)
            : CameraUtils::buildOrthographic(m_orthographicSize, m_aspectRatio, m_nearClip, m_farClip);
        m_projection._31 += m_projectionJitter.x;
        m_projection._32 += m_projectionJitter.y;
        m_inverseProjection = m_projection.Invert();
        m_projectionDirty = false;
        m_viewProjectionDirty = true;
        m_frustumDirty = true;
    }

    void CameraComponent::synchronizeViewProjection() const noexcept
    {
        synchronizeView();
        synchronizeProjection();
        if (!m_viewProjectionDirty)
            return;

        m_viewProjection = m_view * m_projection;
        m_inverseViewProjection = m_viewProjection.Invert();
        m_viewProjectionDirty = false;
    }

    Viewport CameraComponent::getPixelViewport() const noexcept
    {
        return CameraUtils::toPixelViewport(m_viewport, m_renderTargetSize);
    }
} // namespace Engine