#include "Pch.h"
#include "Core\GameObject\GameObject.h"
#include "Core\GameObject\GameObjectManager.h"

namespace Engine
{
    GameObject::GameObject(GameObjectManager& manager, const ObjectGUID guid, std::string name)
        : m_manager(&manager), m_guid(guid), m_name(std::move(name))
    {
    }

    GameObject::~GameObject()
    {
        detachFromParent();
        for (GameObject* child : m_children)
            child->m_parent = nullptr;
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->setGameObject(nullptr);
        }
    }

    bool GameObject::isActiveInHierarchy() const noexcept
    {
        return m_activeSelf && (m_parent == nullptr || m_parent->isActiveInHierarchy());
    }

    void GameObject::setActive(const bool active) noexcept
    {
        const bool wasActive = isActiveInHierarchy();
        m_activeSelf = active;
        const bool isActive = isActiveInHierarchy();
        if (wasActive != isActive)
            propagateActiveState(wasActive, isActive);
    }

    const Transform& GameObject::getWorldTransform() const noexcept
    {
        updateWorldTransform();
        return m_worldTransform;
    }

    Vector3 GameObject::getWorldPosition() const noexcept
    {
        return getWorldTransform().getPosition();
    }

    Quaternion GameObject::getWorldRotation() const noexcept
    {
        return getWorldTransform().getRotation();
    }

    Vector3 GameObject::getWorldScale() const noexcept
    {
        return getWorldTransform().getScale();
    }

    Matrix GameObject::getWorldMatrix() const noexcept
    {
        return getWorldTransform().toMatrix();
    }

    bool GameObject::setParent(GameObject* parent, const bool worldPositionStays) noexcept
    {
        if (parent == this || (parent != nullptr && parent->isDescendantOf(*this)))
            return false;
        if (m_parent == parent)
            return true;

        const Transform worldTransform = getWorldTransform();
        detachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
            m_parent->m_children.push_back(this);
        if (worldPositionStays)
        {
            m_transform = m_parent == nullptr
                ? worldTransform
                : Transform::fromMatrix(worldTransform.toMatrix() * m_parent->getWorldMatrix().Invert());
            m_transform.markDirty();
        }
        m_cachedLocalRevision = 0;
        return true;
    }

    GameObject* GameObject::getChild(const std::size_t index) noexcept
    {
        return index < m_children.size() ? m_children[index] : nullptr;
    }

    const GameObject* GameObject::getChild(const std::size_t index) const noexcept
    {
        return index < m_children.size() ? m_children[index] : nullptr;
    }

    GameObject* GameObject::find(const std::string& name) noexcept
    {
        for (GameObject* child : m_children)
        {
            if (child->m_name == name)
                return child;
            if (GameObject* result = child->find(name))
                return result;
        }
        return nullptr;
    }

    void GameObject::destroy() noexcept
    {
        if (!m_destroyRequested && m_manager != nullptr)
        {
            m_destroyRequested = true;
            m_manager->destroy(this);
        }
    }

    void GameObject::initializeLifecycle() noexcept
    {
        if (!m_lifecycleAwake)
        {
            m_lifecycleAwake = true;
            for (auto& [type, components] : m_components)
            {
                GE_UNUSED(type);
                for (auto& component : components)
                    component->invokeAwake();
            }
        }
        for (GameObject* child : m_children)
            child->initializeLifecycle();
        if (isActiveInHierarchy())
        {
            for (auto& [type, components] : m_components)
            {
                GE_UNUSED(type);
                for (auto& component : components)
                    if (component->isEnabled())
                        component->invokeEnable();
            }
        }
    }

    void GameObject::startLifecycle() noexcept
    {
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                if (isActiveInHierarchy() && component->isEnabled())
                    component->invokeStart();
        }
        for (GameObject* child : m_children)
            child->startLifecycle();
    }

    void GameObject::updateLifecycle(const float deltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeUpdate(deltaTime);
        }
        for (GameObject* child : m_children)
            child->updateLifecycle(deltaTime);
    }

    void GameObject::fixedUpdateLifecycle(const float fixedDeltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeFixedUpdate(fixedDeltaTime);
        }
        for (GameObject* child : m_children)
            child->fixedUpdateLifecycle(fixedDeltaTime);
    }

    void GameObject::lateUpdateLifecycle(const float deltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeLateUpdate(deltaTime);
        }
        for (GameObject* child : m_children)
            child->lateUpdateLifecycle(deltaTime);
    }

    void GameObject::shutdownLifecycle() noexcept
    {
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
            {
                component->invokeDisable();
                component->invokeDestroy();
            }
        }
    }

    void GameObject::propagateActiveState(const bool wasActive, const bool isActive) noexcept
    {
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
            {
                if (wasActive && !isActive)
                    component->invokeDisable();
                else if (!wasActive && isActive && component->isEnabled())
                    component->invokeEnable();
            }
        }
        for (GameObject* child : m_children)
        {
            const bool childWasActive = wasActive && child->m_activeSelf;
            const bool childIsActive = isActive && child->m_activeSelf;
            if (childWasActive != childIsActive)
                child->propagateActiveState(childWasActive, childIsActive);
        }
    }

    bool GameObject::isDescendantOf(const GameObject& object) const noexcept
    {
        for (const GameObject* current = m_parent; current != nullptr; current = current->m_parent)
        {
            if (current == &object)
                return true;
        }
        return false;
    }

    void GameObject::detachFromParent() noexcept
    {
        if (m_parent == nullptr)
            return;
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    void GameObject::updateWorldTransform() const noexcept
    {
        const std::uint64_t parentRevision = m_parent == nullptr ? 0 : m_parent->getWorldTransform().revision();
        if (m_cachedLocalRevision == m_transform.revision() && m_cachedParentRevision == parentRevision)
            return;

        m_worldTransform = m_parent == nullptr
            ? m_transform
            : m_transform.combine(m_parent->getWorldTransform());
        m_worldTransform.markDirty();
        m_cachedLocalRevision = m_transform.revision();
        m_cachedParentRevision = parentRevision;
        ++m_worldRevision;
    }

    Transform* Component::getTransform() noexcept
    {
        return m_gameObject != nullptr ? m_gameObject->getTransform() : nullptr;
    }

    const Transform* Component::getTransform() const noexcept
    {
        return m_gameObject != nullptr ? m_gameObject->getTransform() : nullptr;
    }

    void Component::setEnabled(const bool enabled) noexcept
    {
        if (m_enabled == enabled)
            return;
        const bool wasActive = m_enabled && m_gameObject != nullptr && m_gameObject->isActiveInHierarchy();
        m_enabled = enabled;
        const bool isActive = m_enabled && m_gameObject != nullptr && m_gameObject->isActiveInHierarchy();
        if (wasActive && !isActive)
            invokeDisable();
        else if (!wasActive && isActive)
            invokeEnable();
    }

    void Component::invokeAwake() noexcept
    {
        if (m_lifecycleEnabled && !m_awakened)
        {
            m_awakened = true;
            onAwake();
        }
    }

    void Component::invokeEnable() noexcept
    {
        if (m_lifecycleEnabled && m_awakened && m_enabled && !m_lifecycleActive)
        {
            m_lifecycleActive = true;
            onEnable();
        }
    }

    void Component::invokeStart() noexcept
    {
        if (m_lifecycleEnabled && m_lifecycleActive && !m_started)
        {
            m_started = true;
            onStart();
        }
    }

    void Component::invokeUpdate(const float deltaTime) noexcept
    {
        if (m_lifecycleEnabled && m_lifecycleActive && m_started)
            onUpdate(deltaTime);
    }

    void Component::invokeFixedUpdate(const float fixedDeltaTime) noexcept
    {
        if (m_lifecycleEnabled && m_lifecycleActive && m_started)
            onFixedUpdate(fixedDeltaTime);
    }

    void Component::invokeLateUpdate(const float deltaTime) noexcept
    {
        if (m_lifecycleEnabled && m_lifecycleActive && m_started)
            onLateUpdate(deltaTime);
    }

    void Component::invokeDisable() noexcept
    {
        if (m_lifecycleEnabled && m_lifecycleActive)
        {
            m_lifecycleActive = false;
            onDisable();
        }
    }

    void Component::invokeDestroy() noexcept
    {
        if (m_lifecycleEnabled && m_awakened)
            onDestroy();
    }
} // namespace Engine