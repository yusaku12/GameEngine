#include "Pch.h"

#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    GameObject::GameObject() : m_name("GameObject")
    {
        m_transform = Transform();
    }

    GameObject::GameObject(std::string name) : m_name(std::move(name))
    {
        m_transform = Transform();
    }

    GameObject::GameObject(std::string name, ECSWorld* world, Entity entity)
        : m_name(std::move(name)), m_world(world), m_entity(entity)
    {
        m_transform = Transform();
        if (m_world != nullptr && m_world->IsValid(m_entity) && !m_world->HasComponent<TransformComponent>(m_entity))
            m_world->AddComponent<TransformComponent>(m_entity, m_transform);
    }

    GameObject::~GameObject()
    {
        if (m_world && m_world->IsValid(m_entity))
            m_world->DestroyEntity(m_entity);
    }

    void GameObject::SetParent(GameObjectHandle parent)
    {
        if (m_handle == parent)
        {
            m_parentHandle = {};
            m_parent = nullptr;
            return;
        }

        m_parentHandle = parent;
        m_parent = nullptr;
    }

    void GameObject::AddChild(GameObjectHandle child)
    {
        if (!child.IsValid())
            return;

        if (std::find(m_children.begin(), m_children.end(), child) != m_children.end())
            return;

        m_children.push_back(child);
    }

    void GameObject::RemoveChild(GameObjectHandle child)
    {
        auto it = std::remove(m_children.begin(), m_children.end(), child);
        if (it != m_children.end())
            m_children.erase(it, m_children.end());
    }

    void GameObject::Update(float deltaTime)
    {
        GE_UNUSED(deltaTime);
    }

    void GameObject::FixedUpdate(float fixedDeltaTime)
    {
        GE_UNUSED(fixedDeltaTime);
    }

    void GameObject::LateUpdate(float deltaTime)
    {
        GE_UNUSED(deltaTime);
    }

    void GameObject::Destroy()
    {
        if (m_destroyed)
            return;

        m_destroyed = true;
        if (m_world && m_world->IsValid(m_entity))
            m_world->DestroyEntity(m_entity);
        m_children.clear();
    }

    GameObject& Component::GetGameObject() noexcept
    {
        return *m_gameObject;
    }

    const GameObject& Component::GetGameObject() const noexcept
    {
        return *m_gameObject;
    }
} // namespace Engine
