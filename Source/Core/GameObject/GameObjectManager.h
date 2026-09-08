#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core\CoreDefines.h"
#include "Core\GameObject\GameObject.h"

namespace Engine
{
    /**
     * @brief GameObjectの生成・GUID検索・遅延破棄を管理するクラス。
     * @thread_safety Main thread only. Worker threadからの構造変更は禁止します。
     */
    class GameObjectManager
    {
    public:
        GameObjectManager() = default;
        ~GameObjectManager() = default;

        GE_DISABLE_COPY_AND_MOVE(GameObjectManager);

        [[nodiscard]] GameObject* create(const std::string& name = "GameObject");
        [[nodiscard]] GameObject* create(const std::string& name, ObjectGUID guid);
        void destroy(GameObject* object) noexcept;
        bool transferTo(GameObject* object, GameObjectManager& destination) noexcept;
        void processDestroyQueue() noexcept;
        void clear() noexcept;
        void update(float deltaTime) noexcept;
        void fixedUpdate(float fixedDeltaTime) noexcept;
        void lateUpdate(float deltaTime) noexcept;

        [[nodiscard]] GameObject* find(const std::string& name) noexcept;
        [[nodiscard]] const GameObject* find(const std::string& name) const noexcept;
        [[nodiscard]] GameObject* find(const ObjectGUID& guid) noexcept;
        [[nodiscard]] const GameObject* find(const ObjectGUID& guid) const noexcept;
        [[nodiscard]] GameObject* findWithTag(TagID tag) noexcept;
        [[nodiscard]] std::vector<GameObject*> findGameObjectsWithTag(TagID tag) noexcept;
        [[nodiscard]] std::vector<GameObject*> findGameObjectsInLayer(LayerID layer) noexcept;
        [[nodiscard]] std::size_t size() const noexcept { return m_objects.size(); }
        [[nodiscard]] const auto& objects() const noexcept { return m_objects; }

    private:
        std::vector<std::unique_ptr<GameObject>> m_objects;
        std::vector<GameObject*> m_destroyQueue;
        std::unordered_map<ObjectGUID, GameObject*, ObjectGUIDHash> m_guidIndex;
    };
} // namespace Engine