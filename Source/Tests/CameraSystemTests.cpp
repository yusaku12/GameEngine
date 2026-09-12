#include "Pch.h"
#include "Tests\CameraSystemTests.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Math\Geometry.h"
#include "Core\Scene\Scene.h"
#include "Graphics\Camera\CameraManager.h"

namespace Engine::Tests
{
    namespace
    {
        constexpr float TEST_EPSILON = 0.01f;

        bool nearlyEqual(const float left, const float right) noexcept
        {
            return std::fabs(left - right) <= TEST_EPSILON;
        }

        bool nearlyEqual(const Vector3& left, const Vector3& right) noexcept
        {
            return Vector3::Distance(left, right) <= TEST_EPSILON;
        }

        bool isFinite(const Matrix& matrix) noexcept
        {
            const float* const values = &matrix._11;
            return std::all_of(values, values + 16,
                [](const float value) { return std::isfinite(value); });
        }
    }

    bool runCameraSystemTests()
    {
        CameraManager& cameraManager = CameraManager::instance();
        cameraManager.shutdown();
        cameraManager.setRenderTargetSize(1280, 720);

        Scene scene(ObjectGUID::generate(), "Camera System Tests");
        GameObject* const object = scene.createGameObject("Test Camera");
        if (object == nullptr)
            return false;
        object->getTransform()->setPosition(Vector3(0.0f, 0.0f, -5.0f));

        CameraComponent* const camera = object->addComponent<CameraComponent>();
        if (camera == nullptr)
            return false;
        cameraManager.registerCamera(camera);
        if (!cameraManager.setMainCamera(camera) || cameraManager.getActiveCamera() != camera)
            return false;

        camera->setProjectionMode(CameraProjectionMode::Perspective);
        camera->setFieldOfView(60.0f);
        camera->setNearClipPlane(0.1f);
        camera->setFarClipPlane(1000.0f);
        camera->setAspectRatio(16.0f / 9.0f);
        if (!isFinite(camera->getProjectionMatrix()))
            return false;

        const Vector3 screenCenter = camera->worldToScreenPoint(Vector3::Zero);
        if (!nearlyEqual(screenCenter.x, 640.0f) || !nearlyEqual(screenCenter.y, 360.0f))
            return false;
        if (!nearlyEqual(camera->screenToWorldPoint(screenCenter), Vector3::Zero))
            return false;

        const Ray centerRay = camera->screenPointToRay(Vector2(640.0f, 360.0f));
        if (!nearlyEqual(centerRay.direction, camera->getForward()))
            return false;

        const AABB visibleBounds = makeAABB(Vector3(-0.5f), Vector3(0.5f));
        const AABB hiddenBounds = makeAABB(
            Vector3(-0.5f, -0.5f, -11.0f), Vector3(0.5f, 0.5f, -10.0f));
        if (!isVisible(camera->getFrustum(), visibleBounds)
            || isVisible(camera->getFrustum(), hiddenBounds))
        {
            return false;
        }

        camera->setProjectionMode(CameraProjectionMode::Orthographic);
        camera->setOrthographicSize(5.0f);
        camera->setFarClipPlane(100.0f);
        if (!isFinite(camera->getProjectionMatrix()))
            return false;

        cameraManager.setRenderTargetSize(800, 800);
        if (!nearlyEqual(camera->getAspectRatio(), 1.0f))
            return false;

        scene.destroyGameObject(object);
        scene.processDestroyQueue();
        const bool destroyedCleanly = cameraManager.getMainCamera() == nullptr
            && cameraManager.getActiveCamera() == nullptr;
        cameraManager.shutdown();
        return destroyedCleanly;
    }
} // namespace Engine::Tests