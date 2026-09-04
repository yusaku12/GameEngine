#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_map>
#include <utility>

#include "Core\GameObject\GameObjectHandle.h"
#include "Core\GameObject\Component.h"
#include "Core\ECS\ECSWorld.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    class GameObjectManager;

    /**
     * @brief UnityライクなGameObjectの高レベル表現。
     */
    class GameObject
    {
    public:
        GameObject();
        explicit GameObject(std::string name);
        GameObject(std::string name, ECSWorld* world, Entity entity);
        ~GameObject();

        GE_DISABLE_COPY(GameObject);
        GameObject(GameObject&&) noexcept = default;
        GameObject& operator=(GameObject&&) noexcept = default;

        [[nodiscard]] const std::string& GetName() const noexcept { return m_name; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }

        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

        [[nodiscard]] Transform& GetTransform() noexcept
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
                return m_world->GetComponent<TransformComponent>(m_entity).transform;
            return m_transform;
        }

        [[nodiscard]] const Transform& GetTransform() const noexcept
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
                return m_world->GetComponent<TransformComponent>(m_entity).transform;
            return m_transform;
        }

        [[nodiscard]] Transform GetWorldTransform() const
        {
            const Transform& localTransform = GetTransform();
            if (m_parent == nullptr)
                return localTransform;
            return localTransform.combine(m_parent->GetWorldTransform());
        }

        void SetPosition(const Vector3& position)
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
            {
                m_world->GetComponent<TransformComponent>(m_entity).transform.setPosition(position);
                return;
            }
            m_transform.setPosition(position);
        }

        [[nodiscard]] Vector3 GetPosition() const
        {
            return GetTransform().getPosition();
        }

        [[nodiscard]] Vector3 GetWorldPosition() const
        {
            return GetWorldTransform().getPosition();
        }

        void Translate(const Vector3& delta)
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
            {
                m_world->GetComponent<TransformComponent>(m_entity).transform.translate(delta);
                return;
            }
            m_transform.translate(delta);
        }

        void SetVelocity(const Vector3& velocity)
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<VelocityComponent>(m_entity))
            {
                m_world->GetComponent<VelocityComponent>(m_entity).velocity = velocity;
                return;
            }

            if (m_world != nullptr && m_world->IsValid(m_entity))
            {
                auto& component = AddComponent<VelocityComponent>(velocity);
                component.velocity = velocity;
            }
        }

        [[nodiscard]] Vector3 GetVelocity() const
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<VelocityComponent>(m_entity))
                return m_world->GetComponent<VelocityComponent>(m_entity).velocity;
            return Vector3::Zero;
        }

        void SetRotation(const Quaternion& rotation)
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
            {
                m_world->GetComponent<TransformComponent>(m_entity).transform.setRotation(rotation);
                return;
            }
            m_transform.setRotation(rotation);
        }

        [[nodiscard]] Quaternion GetRotation() const
        {
            return GetTransform().getRotation();
        }

        [[nodiscard]] Quaternion GetWorldRotation() const
        {
            return GetWorldTransform().getRotation();
        }

        void SetScale(const Vector3& scale)
        {
            if (m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<TransformComponent>(m_entity))
            {
                m_world->GetComponent<TransformComponent>(m_entity).transform.setScale(scale);
                return;
            }
            m_transform.setScale(scale);
        }

        [[nodiscard]] Vector3 GetScale() const
        {
            return GetTransform().getScale();
        }

        [[nodiscard]] Vector3 GetWorldScale() const
        {
            return GetWorldTransform().getScale();
        }

        [[nodiscard]] GameObject* GetParent() noexcept { return m_parent; }
        [[nodiscard]] const GameObject* GetParent() const noexcept { return m_parent; }
        [[nodiscard]] GameObjectHandle GetParentHandle() const noexcept { return m_parentHandle; }
        [[nodiscard]] const std::vector<GameObjectHandle>& GetChildren() const noexcept { return m_children; }

        void SetParent(GameObjectHandle parent);
        void AddChild(GameObjectHandle child);
        void RemoveChild(GameObjectHandle child);

        template <typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            if (m_world == nullptr || !m_world->IsValid(m_entity))
                throw std::runtime_error("GameObject is not bound to a valid ECS entity.");

            auto& component = m_world->AddComponent<T>(m_entity, std::forward<Args>(args)...);
            component.m_gameObject = this;
            component.Initialize();
            return component;
        }

        template <typename T>
        T* GetComponent()
        {
            if (m_world == nullptr || !m_world->IsValid(m_entity) || !m_world->HasComponent<T>(m_entity))
                return nullptr;
            return &m_world->GetComponent<T>(m_entity);
        }

        template <typename T>
        const T* GetComponent() const
        {
            if (m_world == nullptr || !m_world->IsValid(m_entity) || !m_world->HasComponent<T>(m_entity))
                return nullptr;
            return &m_world->GetComponent<T>(m_entity);
        }

        template <typename T>
        [[nodiscard]] bool HasComponent() const
        {
            return m_world != nullptr && m_world->IsValid(m_entity) && m_world->HasComponent<T>(m_entity);
        }

        template <typename T>
        void RemoveComponent()
        {
            if (m_world == nullptr || !m_world->IsValid(m_entity))
                return;
            if (!m_world->HasComponent<T>(m_entity))
                return;

            auto& component = m_world->GetComponent<T>(m_entity);
            component.OnDestroy();
            m_world->RemoveComponent<T>(m_entity);
        }

        void Update(float deltaTime);
        void FixedUpdate(float fixedDeltaTime);
        void LateUpdate(float deltaTime);

        void Destroy();
        [[nodiscard]] bool IsDestroyed() const noexcept { return m_destroyed; }

        [[nodiscard]] GameObjectHandle GetHandle() const noexcept { return m_handle; }
        void SetHandle(GameObjectHandle handle) noexcept { m_handle = handle; }

    private:
        std::string m_name;
        bool m_active = true;
        bool m_destroyed = false;
        GameObjectHandle m_handle{};
        GameObjectHandle m_parentHandle{};
        GameObject* m_parent = nullptr;
        std::vector<GameObjectHandle> m_children;
        Transform m_transform;
        ECSWorld* m_world = nullptr;
        Entity m_entity = entt::null;

        friend class GameObjectManager;
    };
} // namespace Engine
