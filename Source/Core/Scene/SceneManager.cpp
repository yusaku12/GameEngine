#include "Pch.h"
#include "Core\Scene\SceneManager.h"

namespace Engine
{
    SceneManager::SceneManager()
        : m_persistentScene(ObjectGUID::generate(), "Persistent")
    {
    }

    void SceneManager::shutdown() noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_persistentScene.isBusy()
            || std::any_of(m_scenes.begin(), m_scenes.end(),
                [](const std::unique_ptr<Scene>& scene) { return scene->isBusy(); }))
        {
            LOG_ERROR("Cannot shut down SceneManager while Scene callbacks or operations are active.");
            return;
        }
        if (m_isShuttingDown)
            return;
        m_isShuttingDown = true;
        m_pendingOperations.clear();
        m_activeScene = nullptr;
        for (const auto& scene : m_scenes)
            scene->clear();
        m_scenes.clear();
        m_persistentScene.clear();
    }

    Scene* SceneManager::createScene(const std::string& name)
    {
        if (m_isShuttingDown || name.empty() || std::any_of(m_scenes.begin(), m_scenes.end(),
            [&name](const std::unique_ptr<Scene>& scene) { return scene->getName() == name; }))
            return nullptr;

        auto scene = std::make_unique<Scene>(ObjectGUID::generate(), name);
        Scene* result = scene.get();
        m_scenes.push_back(std::move(scene));
        if (m_activeScene == nullptr)
            m_activeScene = result;
        return result;
    }

    bool SceneManager::destroyScene(const std::string& name) noexcept
    {
        return removeScene(name);
    }

    bool SceneManager::loadScene(const std::string& name) noexcept
    {
        Scene* scene = findScene(name);
        return scene != nullptr && setActiveScene(scene);
    }

    bool SceneManager::unloadScene(const std::string& name) noexcept
    {
        return removeScene(name);
    }

    bool SceneManager::removeScene(const std::string& name) noexcept
    {
        const auto found = std::find_if(m_scenes.begin(), m_scenes.end(),
            [&name](const std::unique_ptr<Scene>& scene) { return scene->getName() == name; });
        if (found == m_scenes.end())
            return false;
        return removeScene(found->get());
    }

    bool SceneManager::removeScene(Scene* const scene) noexcept
    {
        if (scene == nullptr || scene == m_sceneBeingRemoved || m_isShuttingDown
            || std::none_of(m_scenes.begin(), m_scenes.end(),
                [scene](const std::unique_ptr<Scene>& candidate) { return candidate.get() == scene; }))
        {
            return false;
        }
        if (m_isDispatching || m_isApplyingOperations || scene->isBusy())
            return queueOperation(PendingOperationKind::Remove, scene);

        m_isApplyingOperations = true;
        const bool removed = removeSceneNow(scene);
        m_isApplyingOperations = false;
        applyPendingOperations();
        return removed;
    }

    bool SceneManager::removeSceneNow(Scene* const scene) noexcept
    {
        const auto found = std::find_if(m_scenes.begin(), m_scenes.end(),
            [scene](const std::unique_ptr<Scene>& candidate) { return candidate.get() == scene; });
        if (found == m_scenes.end() || (*found)->isBusy())
            return false;
        m_sceneBeingRemoved = scene;
        if (scene == m_activeScene)
        {
            (*found)->deactivateLifecycle();
            m_activeScene = nullptr;
        }
        m_scenes.erase(found);
        m_sceneBeingRemoved = nullptr;
        if (m_activeScene == nullptr && !m_scenes.empty())
            m_activeScene = m_scenes.front().get();
        return true;
    }

    bool SceneManager::setActiveScene(Scene* scene) noexcept
    {
        if (scene == nullptr || scene == m_sceneBeingRemoved || m_isShuttingDown)
            return false;
        const bool isManaged = std::any_of(m_scenes.begin(), m_scenes.end(),
            [scene](const std::unique_ptr<Scene>& candidate) { return candidate.get() == scene; });
        if (!isManaged)
            return false;

        if (m_isDispatching || m_isApplyingOperations
            || (m_activeScene != nullptr && m_activeScene->isBusy()) || scene->isBusy())
        {
            for (const PendingOperation& operation : m_pendingOperations)
            {
                if (operation.kind == PendingOperationKind::Remove && operation.scene == scene)
                    return false;
            }
            return queueOperation(PendingOperationKind::SetActive, scene);
        }

        if (m_activeScene == scene)
            return true;
        m_isApplyingOperations = true;
        const bool activated = setActiveSceneNow(scene);
        m_isApplyingOperations = false;
        applyPendingOperations();
        return activated;
    }

    bool SceneManager::setActiveSceneNow(Scene* const scene) noexcept
    {
        const bool isManaged = std::any_of(m_scenes.begin(), m_scenes.end(),
            [scene](const std::unique_ptr<Scene>& candidate) { return candidate.get() == scene; });
        if (!isManaged || scene->isBusy() || (m_activeScene != nullptr && m_activeScene->isBusy()))
            return false;
        if (m_activeScene == scene)
            return true;
        if (m_activeScene != nullptr)
            m_activeScene->deactivateLifecycle();
        m_activeScene = scene;
        return true;
    }

    bool SceneManager::queueOperation(const PendingOperationKind kind, Scene* const scene) noexcept
    {
        if (kind == PendingOperationKind::Remove)
        {
            if (std::any_of(m_pendingOperations.begin(), m_pendingOperations.end(),
                [scene](const PendingOperation& operation)
                {
                    return operation.kind == PendingOperationKind::Remove && operation.scene == scene;
                }))
            {
                return true;
            }
        }

        try
        {
            m_pendingOperations.push_back({ kind, scene });
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to defer a Scene operation because memory allocation failed.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Deferred Scene operation queue reached its maximum capacity.");
        }
        return false;
    }

    void SceneManager::applyPendingOperations() noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_isShuttingDown)
            return;

        m_isApplyingOperations = true;
        std::size_t operationIndex = 0;
        std::size_t deferredOperationCount = 0;
        while (operationIndex < m_pendingOperations.size())
        {
            const PendingOperation operation = m_pendingOperations[operationIndex++];
            const auto found = std::find_if(m_scenes.begin(), m_scenes.end(),
                [&operation](const std::unique_ptr<Scene>& scene) { return scene.get() == operation.scene; });
            if (found == m_scenes.end())
                continue;

            const bool sceneBusy = (*found)->isBusy();
            const bool activeSceneBusy = m_activeScene != nullptr && m_activeScene->isBusy();
            if (sceneBusy || (operation.kind == PendingOperationKind::SetActive && activeSceneBusy))
            {
                m_pendingOperations[deferredOperationCount++] = operation;
                continue;
            }

            if (operation.kind == PendingOperationKind::Remove)
                removeSceneNow(operation.scene);
            else
                setActiveSceneNow(operation.scene);
        }
        m_pendingOperations.erase(
            m_pendingOperations.begin() + static_cast<std::ptrdiff_t>(deferredOperationCount),
            m_pendingOperations.end());
        m_isApplyingOperations = false;
    }

    bool SceneManager::dontDestroyOnLoad(GameObject* object) noexcept
    {
        return m_activeScene != nullptr && m_activeScene->moveGameObjectTo(object, m_persistentScene);
    }

    Scene* SceneManager::findScene(const std::string& name) noexcept
    {
        const auto found = std::find_if(m_scenes.begin(), m_scenes.end(),
            [&name](const std::unique_ptr<Scene>& scene) { return scene->getName() == name; });
        return found == m_scenes.end() ? nullptr : found->get();
    }

    const Scene* SceneManager::findScene(const std::string& name) const noexcept
    {
        const auto found = std::find_if(m_scenes.begin(), m_scenes.end(),
            [&name](const std::unique_ptr<Scene>& scene) { return scene->getName() == name; });
        return found == m_scenes.end() ? nullptr : found->get();
    }

    void SceneManager::update(const float deltaTime) noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_isShuttingDown)
        {
            LOG_ERROR("Cannot re-enter SceneManager lifecycle dispatch.");
            return;
        }
        m_isDispatching = true;
        m_persistentScene.update(deltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->update(deltaTime);
        m_isDispatching = false;
        applyPendingOperations();
    }

    void SceneManager::fixedUpdate(const float fixedDeltaTime) noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_isShuttingDown)
        {
            LOG_ERROR("Cannot re-enter SceneManager lifecycle dispatch.");
            return;
        }
        m_isDispatching = true;
        m_persistentScene.fixedUpdate(fixedDeltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->fixedUpdate(fixedDeltaTime);
        m_isDispatching = false;
        applyPendingOperations();
    }

    void SceneManager::lateUpdate(const float deltaTime) noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_isShuttingDown)
        {
            LOG_ERROR("Cannot re-enter SceneManager lifecycle dispatch.");
            return;
        }
        m_isDispatching = true;
        m_persistentScene.lateUpdate(deltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->lateUpdate(deltaTime);
        m_isDispatching = false;
        applyPendingOperations();
    }

    void SceneManager::processDestroyQueue() noexcept
    {
        if (m_isDispatching || m_isApplyingOperations || m_isShuttingDown)
        {
            LOG_ERROR("Cannot re-enter SceneManager lifecycle dispatch.");
            return;
        }
        m_isDispatching = true;
        m_persistentScene.processDestroyQueue();
        if (m_activeScene != nullptr)
            m_activeScene->processDestroyQueue();
        m_isDispatching = false;
        applyPendingOperations();
    }
} // namespace Engine