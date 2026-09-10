#pragma once

#include "Core\GameObject\Component.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\Math\Transform.h"
#include "Core\Object\ObjectGUID.h"
#include "Core\Scene\LayerManager.h"
#include "Core\Scene\TagManager.h"

namespace Engine
{
    class GameObjectManager;

    /**
     * @brief Componentを所有し、親子階層を形成するゲームオブジェクト。
     *
     * @note ECS Entityではありません。Scene/Managerから所有される通常のオブジェクトです。
     * @thread_safety Main thread only.
     */
    class GameObject
    {
    public:

        ~GameObject();

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        GameObject(GameObject&&) = delete;
        GameObject& operator=(GameObject&&) = delete;

        [[nodiscard]] const ObjectGUID& getGUID() const noexcept { return m_guid; }
        [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
        void setName(std::string name) { m_name = std::move(name); }
        [[nodiscard]] TagID getTag() const noexcept { return m_tag; }
        void setTag(TagID tag) noexcept { m_tag = tag; }
        [[nodiscard]] LayerID getLayer() const noexcept { return m_layer; }
        void setLayer(LayerID layer) noexcept { m_layer = layer; }

        [[nodiscard]] bool isActiveSelf() const noexcept { return m_activeSelf; }
        [[nodiscard]] bool isActiveInHierarchy() const noexcept;
        void setActive(bool active) noexcept;

        [[nodiscard]] Transform* getTransform() noexcept { return &m_transform; }
        [[nodiscard]] const Transform* getTransform() const noexcept { return &m_transform; }
        [[nodiscard]] const Transform& getWorldTransform() const noexcept;
        [[nodiscard]] Vector3 getWorldPosition() const noexcept;
        [[nodiscard]] Quaternion getWorldRotation() const noexcept;
        [[nodiscard]] Vector3 getWorldScale() const noexcept;
        [[nodiscard]] Matrix getWorldMatrix() const noexcept;

        [[nodiscard]] GameObject* getParent() const noexcept { return m_parent; }
        bool setParent(GameObject* parent, bool worldPositionStays = true) noexcept;
        [[nodiscard]] std::size_t getChildCount() const noexcept { return m_children.size(); }
        [[nodiscard]] GameObject* getChild(std::size_t index) noexcept;
        [[nodiscard]] const GameObject* getChild(std::size_t index) const noexcept;
        [[nodiscard]] GameObject* find(const std::string& name) noexcept;

        void destroy() noexcept;

        template <typename T, typename... Args>
        T* addComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const ComponentTypeInfo* typeInfo = ComponentRegistry::instance().get<T>();
            ComponentRegistry::instance().ensureRegistered<T>();
            typeInfo = ComponentRegistry::instance().get<T>();
            const auto type = std::type_index(typeid(T));
            auto& components = m_components[type];
            if (!typeInfo->allowMultiple && !components.empty())
                return static_cast<T*>(components.front().get());

            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* result = component.get();
            result->setGameObject(this);
            result->setLifecycleEnabled(typeInfo->executeLifecycle);
            components.push_back(std::move(component));
            if (m_lifecycleAwake)
            {
                result->invokeAwake();
                if (isActiveInHierarchy() && result->isEnabled())
                    result->invokeEnable();
            }
            return result;
        }

        template <typename T>
        [[nodiscard]] T* getComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const auto found = m_components.find(std::type_index(typeid(T)));
            return found == m_components.end() || found->second.empty()
                ? nullptr : static_cast<T*>(found->second.front().get());
        }

        template <typename T>
        [[nodiscard]] const T* getComponent() const noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const auto found = m_components.find(std::type_index(typeid(T)));
            return found == m_components.end() || found->second.empty()
                ? nullptr : static_cast<const T*>(found->second.front().get());
        }

        template <typename T>
        [[nodiscard]] bool hasComponent() const noexcept { return getComponent<T>() != nullptr; }

        template <typename T>
        bool removeComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            return m_components.erase(std::type_index(typeid(T))) != 0;
        }

        [[nodiscard]] std::vector<std::string_view> getComponentTypeNames() const
        {
            std::vector<std::string_view> result;
            for (const auto& [type, components] : m_components)
            {
                if (!components.empty())
                {
                    if (const ComponentTypeInfo* info = ComponentRegistry::instance().get(type))
                        result.push_back(info->name);
                }
            }
            return result;
        }

    private:
        friend class GameObjectManager;

        GameObject(GameObjectManager& manager, ObjectGUID guid, std::string name);
        [[nodiscard]] bool isDescendantOf(const GameObject& object) const noexcept;
        void detachFromParent() noexcept;
        void updateWorldTransform() const noexcept;
        void initializeLifecycle() noexcept;
        void startLifecycle() noexcept;
        void updateLifecycle(float deltaTime) noexcept;
        void fixedUpdateLifecycle(float fixedDeltaTime) noexcept;
        void lateUpdateLifecycle(float deltaTime) noexcept;
        void shutdownLifecycle() noexcept;
        void propagateActiveState(bool wasActive, bool isActive) noexcept;

        GameObjectManager* m_manager = nullptr;
        ObjectGUID m_guid;
        std::string m_name;
        TagID m_tag = 0;
        LayerID m_layer = 0;
        bool m_activeSelf = true;
        bool m_destroyRequested = false;
        Transform m_transform;
        GameObject* m_parent = nullptr;
        std::vector<GameObject*> m_children;
        std::unordered_map<std::type_index, std::vector<std::unique_ptr<Component>>> m_components;
        mutable Transform m_worldTransform;
        mutable std::uint64_t m_cachedLocalRevision = 0;
        mutable std::uint64_t m_cachedParentRevision = 0;
        mutable std::uint64_t m_worldRevision = 0;
        bool m_lifecycleAwake = false;
    };
} // namespace Engine