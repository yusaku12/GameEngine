#include "Pch.h"
#include "Graphics\Camera\CameraManager.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\GameObject.h"

namespace Engine
{
    CameraManager& CameraManager::instance() noexcept
    {
        static CameraManager instance;
        return instance;
    }

    void CameraManager::registerCamera(CameraComponent* camera)
    {
        if (camera == nullptr || contains(camera))
            return;

        m_cameras.push_back(camera);
        camera->setRenderTargetSize(m_renderTargetWidth, m_renderTargetHeight);
        if (m_mainCamera == nullptr)
            m_mainCamera = camera;
    }

    void CameraManager::unregisterCamera(CameraComponent* camera) noexcept
    {
        std::erase(m_cameras, camera);
        if (m_mainCamera == camera)
            m_mainCamera = nullptr;
        if (m_editorCamera == camera)
        {
            m_editorCamera = nullptr;
            m_editorCameraActive = false;
        }
    }

    bool CameraManager::setMainCamera(CameraComponent* camera) noexcept
    {
        if (camera != nullptr && !contains(camera))
            return false;
        m_mainCamera = camera;
        return true;
    }

    bool CameraManager::setEditorCamera(CameraComponent* camera) noexcept
    {
        if (camera != nullptr && !contains(camera))
            return false;
        m_editorCamera = camera;
        if (camera == nullptr)
            m_editorCameraActive = false;
        return true;
    }

    CameraComponent* CameraManager::getActiveCamera() const noexcept
    {
        const auto isUsable = [](const CameraComponent* camera)
            {
                return camera != nullptr && camera->isEnabled()
                    && camera->getGameObject() != nullptr
                    && camera->getGameObject()->isActiveInHierarchy();
            };

        if (m_editorCameraActive && isUsable(m_editorCamera))
            return m_editorCamera;
        return isUsable(m_mainCamera) ? m_mainCamera : nullptr;
    }

    void CameraManager::setRenderTargetSize(const std::uint32_t width, const std::uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
            return;

        m_renderTargetWidth = width;
        m_renderTargetHeight = height;
        for (CameraComponent* camera : m_cameras)
            camera->setRenderTargetSize(width, height);
    }

    void CameraManager::shutdown() noexcept
    {
        m_cameras.clear();
        m_mainCamera = nullptr;
        m_editorCamera = nullptr;
        m_editorCameraActive = false;
    }

    bool CameraManager::contains(const CameraComponent* camera) const noexcept
    {
        return std::ranges::find(m_cameras, camera) != m_cameras.end();
    }
} // namespace Engine