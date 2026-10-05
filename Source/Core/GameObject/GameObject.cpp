#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\GameObject\GameObject.h"
#include "Core\GameObject\GameObjectManager.h"

namespace Engine
{
    GameObject::GameObject(GameObjectManager& manager, const ObjectGUID guid, std::string name)
        : m_manager(&manager), m_guid(guid), m_name(std::move(name))
    {
        ComponentRegistry::instance().registerType<TransformComponent>("Transform", false, true, false);
        m_transformComponent = addComponent<TransformComponent>();
    }

    GameObject::~GameObject()
    {
        detachFromParent();
        for (PendingComponentOperation& operation : m_pendingComponentOperations)
        {
            if (operation.component != nullptr)
                operation.component->setGameObject(nullptr);
        }
        for (GameObject* child : m_children)
            child->m_parent = nullptr;
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->setGameObject(nullptr);
        }
    }

    Component* GameObject::addComponent(const std::string_view typeName)
    {
        const ComponentTypeInfo* info = ComponentRegistry::instance().findByName(typeName);
        if (info == nullptr || info->factory == nullptr)
            return nullptr;
        if (m_lifecycleShuttingDown)
        {
            LOG_ERROR("Cannot add a Component while its GameObject is shutting down.");
            return nullptr;
        }
        auto type = ComponentRegistry::instance().create(typeName);
        if (type == nullptr)
            return nullptr;
        const std::type_index typeIndex(typeid(*type));
        if (!info->allowMultiple)
        {
            if (Component* const existing = findComponentForAddition(typeIndex))
                return existing;
        }

        Component* result = type.get();
        result->setGameObject(this);
        result->setLifecycleEnabled(info->executeLifecycle);
        if (m_lifecycleDispatchDepth != 0)
        {
            if (!queueComponentAddition(typeIndex, type))
                return nullptr;
        }
        else if (!storeComponent(typeIndex, type))
        {
            return nullptr;
        }
        if (m_lifecycleAwake)
        {
            if (m_lifecycleDispatchDepth == 0)
            {
                const LifecycleDispatchScope dispatch(*this);
                result->invokeAwake();
                if (isActiveInHierarchy() && result->isEnabled())
                    result->invokeEnable();
            }
            if (m_lifecycleDispatchDepth == 0 && !containsComponent(result))
                return nullptr;
        }
        return result;
    }

    bool GameObject::containsComponent(const Component* const component) const noexcept
    {
        if (component == nullptr)
            return false;
        for (const auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            if (std::any_of(components.begin(), components.end(),
                [component](const std::unique_ptr<Component>& candidate) { return candidate.get() == component; }))
                return true;
        }
        return false;
    }

    Component* GameObject::findComponentForAddition(const std::type_index& type) const noexcept
    {
        Component* result = nullptr;
        if (const auto found = m_components.find(type); found != m_components.end() && !found->second.empty())
            result = found->second.front().get();

        for (const PendingComponentOperation& operation : m_pendingComponentOperations)
        {
            if (operation.type != type)
                continue;
            if (operation.kind == PendingComponentOperationKind::Remove)
            {
                result = nullptr;
            }
            else if (operation.component != nullptr)
            {
                result = operation.component.get();
            }
        }
        return result;
    }

    bool GameObject::hasComponentAfterPendingOperations(const std::type_index& type) const noexcept
    {
        const auto found = m_components.find(type);
        bool exists = found != m_components.end() && !found->second.empty();
        for (const PendingComponentOperation& operation : m_pendingComponentOperations)
        {
            if (operation.type == type)
                exists = operation.kind == PendingComponentOperationKind::Add;
        }
        return exists;
    }

    bool GameObject::queueComponentAddition(
        const std::type_index& type,
        std::unique_ptr<Component>& component) noexcept
    {
        try
        {
            m_pendingComponentOperations.emplace_back(
                PendingComponentOperationKind::Add, type, std::move(component));
            return true;
        }
        catch (const std::bad_alloc&)
        {
            if (component != nullptr)
                component->setGameObject(nullptr);
            LOG_ERROR("Failed to defer a Component addition because memory allocation failed.");
        }
        catch (const std::length_error&)
        {
            if (component != nullptr)
                component->setGameObject(nullptr);
            LOG_ERROR("Deferred Component operation queue reached its maximum capacity.");
        }
        return false;
    }

    bool GameObject::storeComponent(
        const std::type_index& type,
        std::unique_ptr<Component>& component) noexcept
    {
        try
        {
            auto& components = m_components[type];
            components.push_back(std::move(component));
            return true;
        }
        catch (const std::bad_alloc&)
        {
            if (component != nullptr)
                component->setGameObject(nullptr);
            const auto emptyEntry = m_components.find(type);
            if (emptyEntry != m_components.end() && emptyEntry->second.empty())
                m_components.erase(emptyEntry);
            LOG_ERROR("Failed to allocate memory while storing a Component.");
        }
        catch (const std::length_error&)
        {
            if (component != nullptr)
                component->setGameObject(nullptr);
            const auto emptyEntry = m_components.find(type);
            if (emptyEntry != m_components.end() && emptyEntry->second.empty())
                m_components.erase(emptyEntry);
            LOG_ERROR("Component collection reached its maximum capacity.");
        }
        return false;
    }

    void GameObject::applyPendingComponentOperations() noexcept
    {
        if (m_lifecycleDispatchDepth != 0)
            return;

        std::size_t operationIndex = 0;
        while (operationIndex < m_pendingComponentOperations.size())
        {
            const PendingComponentOperationKind kind = m_pendingComponentOperations[operationIndex].kind;
            const std::type_index type = m_pendingComponentOperations[operationIndex].type;
            ++operationIndex;

            if (kind == PendingComponentOperationKind::Remove)
            {
                const auto found = m_components.find(type);
                if (found == m_components.end())
                    continue;

                ++m_lifecycleDispatchDepth;
                for (const std::unique_ptr<Component>& component : found->second)
                {
                    component->invokeDisable();
                    component->invokeDestroy();
                    component->setGameObject(nullptr);
                }
                m_components.erase(found);
                --m_lifecycleDispatchDepth;
                continue;
            }

            std::unique_ptr<Component> component =
                std::move(m_pendingComponentOperations[operationIndex - 1].component);
            if (component == nullptr)
                continue;

            Component* const addedComponent = component.get();
            if (!storeComponent(type, component))
                continue;

            ++m_lifecycleDispatchDepth;
            if (m_lifecycleAwake)
            {
                addedComponent->invokeAwake();
                if (isActiveInHierarchy() && addedComponent->isEnabled())
                    addedComponent->invokeEnable();
            }
            --m_lifecycleDispatchDepth;
        }
        m_pendingComponentOperations.clear();
    }

    bool GameObject::isActiveInHierarchy() const noexcept
    {
        return m_activeSelf && (m_parent == nullptr || m_parent->isActiveInHierarchy());
    }

    Transform* GameObject::getTransform() noexcept
    {
        return m_transformComponent == nullptr ? nullptr : &m_transformComponent->localTransform();
    }

    const Transform* GameObject::getTransform() const noexcept
    {
        return m_transformComponent == nullptr ? nullptr : &m_transformComponent->localTransform();
    }

    void GameObject::setActive(const bool active) noexcept
    {
        const bool wasActive = isActiveInHierarchy();
        m_activeSelf = active;
        const bool isActive = isActiveInHierarchy();
        if (wasActive != isActive)
            propagateActiveState(wasActive, isActive);
    }

    void GameObject::setLayer(const LayerID layer) noexcept
    {
        if (layer >= 32)
        {
            LOG_WARNING("Cannot set GameObject Layer to an ID outside the supported range [0, 31]: {}.",
                static_cast<unsigned int>(layer));
            return;
        }
        m_layer = layer;
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
        if (m_destroyRequested
            || (parent != nullptr && (parent->m_manager != m_manager || parent->m_destroyRequested)))
            return false;
        if (parent == this || (parent != nullptr && parent->isDescendantOf(*this)))
            return false;
        if (m_parent == parent)
            return true;

        const bool wasActive = isActiveInHierarchy();
        Transform* localTransform = getTransform();
        if (localTransform == nullptr)
            return false;

        Matrix parentWorldInverse = Matrix::Identity;
        if (worldPositionStays && parent != nullptr)
        {
            const Matrix parentWorld = parent->getWorldMatrix();
            const float determinant = parentWorld.Determinant();
            if (!std::isfinite(determinant) || determinant == 0.0f)
            {
                LOG_ERROR("Cannot preserve world transform when the new parent has a singular world matrix.");
                return false;
            }

            parentWorldInverse = parentWorld.Invert();
            const std::array<float, 16> inverseValues{
                parentWorldInverse._11, parentWorldInverse._12, parentWorldInverse._13, parentWorldInverse._14,
                parentWorldInverse._21, parentWorldInverse._22, parentWorldInverse._23, parentWorldInverse._24,
                parentWorldInverse._31, parentWorldInverse._32, parentWorldInverse._33, parentWorldInverse._34,
                parentWorldInverse._41, parentWorldInverse._42, parentWorldInverse._43, parentWorldInverse._44
            };
            if (!std::ranges::all_of(inverseValues, [](const float value) { return std::isfinite(value); }))
            {
                LOG_ERROR("Cannot preserve world transform because the new parent's inverse world matrix is invalid.");
                return false;
            }
        }

        if (parent != nullptr)
        {
            if (parent->m_children.size() == parent->m_children.max_size())
            {
                LOG_ERROR("Cannot set GameObject parent because the child list reached its maximum size.");
                return false;
            }
            try
            {
                parent->m_children.reserve(parent->m_children.size() + 1);
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("Failed to allocate memory while setting GameObject parent.");
                return false;
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("Cannot set GameObject parent because the child list is too large.");
                return false;
            }
        }

        const Transform worldTransform = getWorldTransform();
        Transform newLocalTransform;
        if (worldPositionStays)
        {
            const Matrix localMatrix = parent == nullptr
                ? worldTransform.toMatrix()
                : worldTransform.toMatrix() * parentWorldInverse;
            if (!Transform::tryFromMatrix(localMatrix, newLocalTransform))
            {
                LOG_ERROR("Cannot preserve world transform because the resulting local matrix cannot be represented as TRS.");
                return false;
            }

            const Vector3& position = newLocalTransform.getPosition();
            const Vector3& scale = newLocalTransform.getScale();
            const Quaternion& rotation = newLocalTransform.getRotation();
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
                || !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)
                || !std::isfinite(rotation.x) || !std::isfinite(rotation.y)
                || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
            {
                LOG_ERROR("Cannot preserve world transform because the resulting local transform is invalid.");
                return false;
            }
        }

        detachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
            m_parent->m_children.push_back(this);
        if (worldPositionStays)
        {
            *localTransform = newLocalTransform;
            localTransform->markDirty();
        }
        m_cachedLocalRevision = 0;
        const bool isActive = isActiveInHierarchy();
        if (wasActive != isActive)
            propagateActiveState(wasActive, isActive);
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
            m_manager->destroy(this);
    }

    void GameObject::initializeLifecycle() noexcept
    {
        const LifecycleDispatchScope dispatch(*this);
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
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->initializeLifecycle();
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
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
        const LifecycleDispatchScope dispatch(*this);
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                if (isActiveInHierarchy() && component->isEnabled())
                    component->invokeStart();
        }
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->startLifecycle();
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
    }

    void GameObject::updateLifecycle(const float deltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        const LifecycleDispatchScope dispatch(*this);
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeUpdate(deltaTime);
        }
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->updateLifecycle(deltaTime);
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
    }

    void GameObject::fixedUpdateLifecycle(const float fixedDeltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        const LifecycleDispatchScope dispatch(*this);
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeFixedUpdate(fixedDeltaTime);
        }
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->fixedUpdateLifecycle(fixedDeltaTime);
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
    }

    void GameObject::lateUpdateLifecycle(const float deltaTime) noexcept
    {
        if (!isActiveInHierarchy())
            return;
        const LifecycleDispatchScope dispatch(*this);
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeLateUpdate(deltaTime);
        }
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->lateUpdateLifecycle(deltaTime);
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
    }

    void GameObject::shutdownLifecycle() noexcept
    {
        if (m_lifecycleShuttingDown)
            return;
        m_lifecycleShuttingDown = true;
        const LifecycleDispatchScope dispatch(*this);
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

    void GameObject::deactivateLifecycle() noexcept
    {
        const LifecycleDispatchScope dispatch(*this);
        for (auto& [type, components] : m_components)
        {
            GE_UNUSED(type);
            for (auto& component : components)
                component->invokeDisable();
        }
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            child->deactivateLifecycle();
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
        }
    }

    void GameObject::propagateActiveState(const bool wasActive, const bool isActive) noexcept
    {
        const LifecycleDispatchScope dispatch(*this);
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
        for (std::size_t childIndex = 0; childIndex < m_children.size();)
        {
            GameObject* const child = m_children[childIndex];
            const bool childWasActive = wasActive && child->m_activeSelf;
            const bool childIsActive = isActive && child->m_activeSelf;
            if (childWasActive != childIsActive)
                child->propagateActiveState(childWasActive, childIsActive);
            if (childIndex < m_children.size() && m_children[childIndex] == child)
                ++childIndex;
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
        const Transform* localTransform = getTransform();
        if (localTransform == nullptr)
            return;
        const std::uint64_t parentRevision = m_parent == nullptr ? 0 : m_parent->getWorldTransform().revision();
        if (m_cachedLocalRevision == localTransform->revision() && m_cachedParentRevision == parentRevision)
            return;

        m_worldTransform = m_parent == nullptr
            ? *localTransform
            : localTransform->combine(m_parent->getWorldTransform());
        m_worldTransform.markDirty();
        m_cachedLocalRevision = localTransform->revision();
        m_cachedParentRevision = parentRevision;
        ++m_worldRevision;
    }

    void GameObject::updateWorldTransformHierarchy() const noexcept
    {
        updateWorldTransform();
        for (const GameObject* const child : m_children)
            child->updateWorldTransformHierarchy();
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
        const auto updateEnabledState = [this, enabled]()
            {
                const bool wasActive = m_enabled && m_gameObject != nullptr && m_gameObject->isActiveInHierarchy();
                m_enabled = enabled;
                const bool isActive = m_enabled && m_gameObject != nullptr && m_gameObject->isActiveInHierarchy();
                if (wasActive && !isActive)
                    invokeDisable();
                else if (!wasActive && isActive)
                    invokeEnable();
            };
        if (m_gameObject != nullptr)
        {
            const GameObject::LifecycleDispatchScope dispatch(*m_gameObject);
            updateEnabledState();
        }
        else
        {
            updateEnabledState();
        }
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
        if (m_lifecycleEnabled && m_awakened && !m_destroyed)
        {
            m_destroyed = true;
            onDestroy();
        }
    }
} // namespace Engine