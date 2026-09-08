#include "Pch.h"
#include "Core\Scene\SceneManager.h"

namespace Engine
{
    SceneManager::SceneManager()
        : m_persistentScene(ObjectGUID::generate(), "Persistent")
    {
    }

    Scene* SceneManager::createScene(const std::string& name)
    {
        if (name.empty() || m_scenes.contains(name))
            return nullptr;

        auto scene = std::make_unique<Scene>(ObjectGUID::generate(), name);
        Scene* result = scene.get();
        m_scenes.emplace(name, std::move(scene));
        if (m_activeScene == nullptr)
            m_activeScene = result;
        return result;
    }

    bool SceneManager::destroyScene(const std::string& name) noexcept
    {
        const auto found = m_scenes.find(name);
        if (found == m_scenes.end())
            return false;
        if (found->second.get() == m_activeScene)
            m_activeScene = nullptr;
        m_scenes.erase(found);
        if (m_activeScene == nullptr && !m_scenes.empty())
            m_activeScene = m_scenes.begin()->second.get();
        return true;
    }

    bool SceneManager::loadScene(const std::string& name) noexcept
    {
        Scene* scene = findScene(name);
        return scene != nullptr && setActiveScene(scene);
    }

    bool SceneManager::unloadScene(const std::string& name) noexcept
    {
        const auto found = m_scenes.find(name);
        if (found == m_scenes.end())
            return false;
        if (found->second.get() == m_activeScene)
            m_activeScene = nullptr;
        m_scenes.erase(found);
        if (m_activeScene == nullptr && !m_scenes.empty())
            m_activeScene = m_scenes.begin()->second.get();
        return true;
    }

    bool SceneManager::setActiveScene(Scene* scene) noexcept
    {
        if (scene == nullptr)
            return false;
        for (const auto& [name, candidate] : m_scenes)
        {
            if (candidate.get() == scene)
            {
                m_activeScene = scene;
                return true;
            }
        }
        return false;
    }

    bool SceneManager::dontDestroyOnLoad(GameObject* object) noexcept
    {
        return m_activeScene != nullptr && m_activeScene->moveGameObjectTo(object, m_persistentScene);
    }

    Scene* SceneManager::findScene(const std::string& name) noexcept
    {
        const auto found = m_scenes.find(name);
        return found == m_scenes.end() ? nullptr : found->second.get();
    }

    const Scene* SceneManager::findScene(const std::string& name) const noexcept
    {
        const auto found = m_scenes.find(name);
        return found == m_scenes.end() ? nullptr : found->second.get();
    }

    void SceneManager::update(const float deltaTime) noexcept
    {
        m_persistentScene.update(deltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->update(deltaTime);
    }

    void SceneManager::fixedUpdate(const float fixedDeltaTime) noexcept
    {
        m_persistentScene.fixedUpdate(fixedDeltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->fixedUpdate(fixedDeltaTime);
    }

    void SceneManager::lateUpdate(const float deltaTime) noexcept
    {
        m_persistentScene.lateUpdate(deltaTime);
        if (m_activeScene != nullptr)
            m_activeScene->lateUpdate(deltaTime);
    }

    void SceneManager::processDestroyQueue() noexcept
    {
        m_persistentScene.processDestroyQueue();
        if (m_activeScene != nullptr)
            m_activeScene->processDestroyQueue();
    }
} // namespace Engine