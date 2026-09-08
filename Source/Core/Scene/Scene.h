#pragma once

#include <string>

#include "Core\GameObject\GameObjectManager.h"

namespace Engine
{
    /**
     * @brief GameObjectの所有単位となるScene。
     * @thread_safety Main thread only.
     */
    class Scene
    {
    public:
        Scene(ObjectGUID guid, std::string name);
        ~Scene() = default;

        GE_DISABLE_COPY_AND_MOVE(Scene);

        [[nodiscard]] const ObjectGUID& getGUID() const noexcept { return m_guid; }
        [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
        void setName(std::string name) { m_name = std::move(name); }
        void setGUID(ObjectGUID guid) noexcept { m_guid = guid; }

        [[nodiscard]] GameObject* createGameObject(const std::string& name = "GameObject")
        {
            return m_objects.create(name);
        }

        [[nodiscard]] GameObject* createGameObject(const std::string& name, ObjectGUID guid)
        {
            return m_objects.create(name, guid);
        }

        void destroyGameObject(GameObject* object) noexcept { m_objects.destroy(object); }
        bool moveGameObjectTo(GameObject* object, Scene& destination) noexcept
        {
            return m_objects.transferTo(object, destination.m_objects);
        }
        void update(float deltaTime) noexcept { m_objects.update(deltaTime); }
        void fixedUpdate(float fixedDeltaTime) noexcept { m_objects.fixedUpdate(fixedDeltaTime); }
        void lateUpdate(float deltaTime) noexcept { m_objects.lateUpdate(deltaTime); }
        void processDestroyQueue() noexcept { m_objects.processDestroyQueue(); }

        [[nodiscard]] GameObject* find(const std::string& name) noexcept { return m_objects.find(name); }
        [[nodiscard]] const GameObject* find(const std::string& name) const noexcept { return m_objects.find(name); }
        [[nodiscard]] GameObject* find(const ObjectGUID& guid) noexcept { return m_objects.find(guid); }
        [[nodiscard]] const GameObject* find(const ObjectGUID& guid) const noexcept { return m_objects.find(guid); }
        [[nodiscard]] GameObject* findWithTag(TagID tag) noexcept { return m_objects.findWithTag(tag); }
        [[nodiscard]] std::vector<GameObject*> findGameObjectsWithTag(TagID tag) noexcept { return m_objects.findGameObjectsWithTag(tag); }
        [[nodiscard]] std::vector<GameObject*> findGameObjectsInLayer(LayerID layer) noexcept { return m_objects.findGameObjectsInLayer(layer); }
        [[nodiscard]] std::size_t getGameObjectCount() const noexcept { return m_objects.size(); }
        [[nodiscard]] const auto& getGameObjects() const noexcept { return m_objects.objects(); }
        void clear() noexcept { m_objects.clear(); }

    private:
        ObjectGUID m_guid;
        std::string m_name;
        GameObjectManager m_objects;
    };
} // namespace Engine