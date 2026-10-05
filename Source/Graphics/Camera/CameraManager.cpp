#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Graphics\Camera\CameraManager.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Graphics\Camera\CameraRenderSubmission.h"

namespace Engine
{
    namespace
    {
        bool isUsableCamera(const CameraComponent* camera) noexcept
        {
            return camera != nullptr && camera->isEnabled()
                && camera->getGameObject() != nullptr
                && camera->getGameObject()->isActiveInHierarchy();
        }
    }

    CameraManager& CameraManager::instance() noexcept
    {
        static CameraManager instance;
        return instance;
    }

    void CameraManager::registerCamera(CameraComponent* camera) noexcept
    {
        if (camera == nullptr || contains(camera))
            return;

        try
        {
            m_cameras.push_back(camera);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while registering a Camera.");
            return;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Camera registry reached its maximum capacity.");
            return;
        }

        camera->setRenderTargetSize(m_renderTargetWidth, m_renderTargetHeight);
        if (m_mainCamera == nullptr)
            m_mainCamera = camera;
    }

    void CameraManager::unregisterCamera(CameraComponent* camera) noexcept
    {
        const bool wasActiveCamera = getActiveCamera() == camera;
        std::erase(m_cameras, camera);
        if (wasActiveCamera)
            CameraRenderSubmissionQueue::instance().clear();
        if (m_mainCamera == camera)
        {
            const auto replacement = std::find_if(m_cameras.begin(), m_cameras.end(), isUsableCamera);
            m_mainCamera = replacement != m_cameras.end()
                ? *replacement : (m_cameras.empty() ? nullptr : m_cameras.front());
        }
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
        if (m_editorCameraActive && isUsableCamera(m_editorCamera))
            return m_editorCamera;
        if (isUsableCamera(m_mainCamera))
            return m_mainCamera;

        const auto fallback = std::find_if(m_cameras.begin(), m_cameras.end(), isUsableCamera);
        return fallback != m_cameras.end() ? *fallback : nullptr;
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