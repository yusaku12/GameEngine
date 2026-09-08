#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Core\CoreDefines.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    /**
     * @brief Sceneの生成・切り替え・破棄を管理するクラス。
     * @thread_safety Main thread only.
     */
    class SceneManager
    {
    public:
        SceneManager();
        ~SceneManager() = default;

        GE_DISABLE_COPY_AND_MOVE(SceneManager);

        [[nodiscard]] Scene* createScene(const std::string& name);
        bool destroyScene(const std::string& name) noexcept;
        bool loadScene(const std::string& name) noexcept;
        bool unloadScene(const std::string& name) noexcept;
        bool setActiveScene(Scene* scene) noexcept;
        bool dontDestroyOnLoad(GameObject* object) noexcept;

        [[nodiscard]] Scene* getActiveScene() noexcept { return m_activeScene; }
        [[nodiscard]] const Scene* getActiveScene() const noexcept { return m_activeScene; }
        [[nodiscard]] Scene* getPersistentScene() noexcept { return &m_persistentScene; }
        [[nodiscard]] Scene* findScene(const std::string& name) noexcept;
        [[nodiscard]] const Scene* findScene(const std::string& name) const noexcept;

        void update(float deltaTime) noexcept;
        void fixedUpdate(float fixedDeltaTime) noexcept;
        void lateUpdate(float deltaTime) noexcept;
        void processDestroyQueue() noexcept;

    private:
        std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
        Scene m_persistentScene;
        Scene* m_activeScene = nullptr;
    };
} // namespace Engine