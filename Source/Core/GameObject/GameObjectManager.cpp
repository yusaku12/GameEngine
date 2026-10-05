#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\GameObject\GameObjectManager.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\Threading\JobSystem.h"

namespace Engine
{
    namespace
    {
        class ScopedFlag
        {
        public:

            explicit ScopedFlag(bool& flag) noexcept
                : m_flag(flag)
            {
                m_flag = true;
            }

            ~ScopedFlag() noexcept
            {
                m_flag = false;
            }

            ScopedFlag(const ScopedFlag&) = delete;
            ScopedFlag& operator=(const ScopedFlag&) = delete;

        private:

            bool& m_flag;
        };
    }

    GameObject* GameObjectManager::create(const std::string& name)
    {
        return create(name, ObjectGUID::generate());
    }

    GameObject* GameObjectManager::create(const std::string& name, const ObjectGUID guid)
    {
        if (m_isClearing)
        {
            LOG_ERROR("Cannot create a GameObject while the manager is clearing.");
            return nullptr;
        }
        if (!guid.isValid() || m_guidIndex.contains(guid))
            return nullptr;

        if (m_objects.size() == m_objects.max_size() || m_guidIndex.size() == m_guidIndex.max_size())
        {
            LOG_ERROR("Cannot create GameObject because the manager reached its maximum capacity.");
            return nullptr;
        }

        if (!reserveUpdateBuffers(m_objects.size() + 1))
            return nullptr;

        bool objectStored = false;
        try
        {
            m_objects.reserve(m_objects.size() + 1);
            m_guidIndex.reserve(m_guidIndex.size() + 1);

            auto object = std::unique_ptr<GameObject>(new GameObject(*this, guid, name));
            if (object->getTransform() == nullptr)
            {
                LOG_ERROR("Cannot create GameObject because its required Transform component could not be registered.");
                return nullptr;
            }

            GameObject* const result = object.get();
            m_objects.push_back(std::move(object));
            objectStored = true;

            const auto [entry, inserted] = m_guidIndex.emplace(guid, result);
            GE_UNUSED(entry);
            if (!inserted)
            {
                m_objects.pop_back();
                LOG_ERROR("Cannot create GameObject because its GUID became unavailable.");
                return nullptr;
            }
            return result;
        }
        catch (const std::bad_alloc&)
        {
            if (objectStored)
                m_objects.pop_back();
            LOG_ERROR("Failed to allocate memory while creating GameObject.");
        }
        catch (const std::length_error&)
        {
            if (objectStored)
                m_objects.pop_back();
            LOG_ERROR("Cannot create GameObject because the manager reached its maximum capacity.");
        }
        return nullptr;
    }

    bool GameObjectManager::destroy(GameObject* object) noexcept
    {
        if (object == nullptr || object->m_manager != this)
            return false;
        if (object->m_destroyRequested)
            return true;
        try
        {
            if (std::find(m_destroyQueue.begin(), m_destroyQueue.end(), object) == m_destroyQueue.end())
                m_destroyQueue.push_back(object);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to queue GameObject for destruction because memory allocation failed.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("GameObject destruction queue reached its maximum capacity.");
            return false;
        }
        object->m_destroyRequested = true;
        return true;
    }

    bool GameObjectManager::transferTo(GameObject* object, GameObjectManager& destination) noexcept
    {
        if (object == nullptr || object->m_manager != this || object->m_parent != nullptr
            || &destination == this || object->m_destroyRequested || m_isClearing || destination.m_isClearing)
            return false;

        std::vector<GameObject*> subtree;
        std::size_t insertedCount = 0;
        const auto rollbackDestinationIndex = [&destination, &subtree, &insertedCount]() noexcept
            {
                for (std::size_t index = 0; index < insertedCount; ++index)
                    destination.m_guidIndex.erase(subtree[index]->m_guid);
            };
        try
        {
            subtree.push_back(object);
            for (std::size_t index = 0; index < subtree.size(); ++index)
            {
                for (GameObject* child : subtree[index]->m_children)
                    subtree.push_back(child);
            }

            if (subtree.size() > destination.m_objects.max_size() - destination.m_objects.size()
                || subtree.size() > destination.m_guidIndex.max_size() - destination.m_guidIndex.size())
                return false;

            if (!destination.reserveUpdateBuffers(destination.m_objects.size() + subtree.size()))
                return false;

            for (GameObject* current : subtree)
            {
                const auto owned = std::find_if(m_objects.begin(), m_objects.end(),
                    [current](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == current; });
                const auto indexed = m_guidIndex.find(current->m_guid);
                if (current->m_destroyRequested || owned == m_objects.end()
                    || indexed == m_guidIndex.end() || indexed->second != current
                    || destination.m_guidIndex.contains(current->m_guid))
                    return false;
            }

            destination.m_objects.reserve(destination.m_objects.size() + subtree.size());
            destination.m_guidIndex.reserve(destination.m_guidIndex.size() + subtree.size());

            for (GameObject* current : subtree)
            {
                const auto [entry, inserted] = destination.m_guidIndex.emplace(current->m_guid, current);
                GE_UNUSED(entry);
                if (!inserted)
                {
                    rollbackDestinationIndex();
                    LOG_ERROR("Failed to transfer GameObject subtree because a GUID became unavailable.");
                    return false;
                }
                ++insertedCount;
            }
        }
        catch (const std::bad_alloc&)
        {
            rollbackDestinationIndex();
            LOG_ERROR("Failed to allocate memory while transferring a GameObject subtree.");
            return false;
        }
        catch (const std::length_error&)
        {
            rollbackDestinationIndex();
            LOG_ERROR("GameObject subtree is too large to transfer.");
            return false;
        }

        for (GameObject* current : subtree)
        {
            m_guidIndex.erase(current->m_guid);
            const auto found = std::find_if(m_objects.begin(), m_objects.end(),
                [current](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == current; });
            std::unique_ptr<GameObject> ownership = std::move(*found);
            m_objects.erase(found);
            current->m_manager = &destination;
            destination.m_objects.push_back(std::move(ownership));
        }
        return true;
    }

    bool GameObjectManager::replaceContentsFrom(GameObjectManager& source) noexcept
    {
        if (&source == this || m_isUpdatingLifecycle || source.m_isUpdatingLifecycle
            || m_isDeactivatingLifecycle || source.m_isDeactivatingLifecycle
            || m_isProcessingDestroyQueue || source.m_isProcessingDestroyQueue
            || m_isClearing || source.m_isClearing || !source.m_destroyQueue.empty()
            || source.m_objects.size() != source.m_guidIndex.size())
            return false;

        for (const std::unique_ptr<GameObject>& object : source.m_objects)
        {
            if (object == nullptr || object->m_manager != &source
                || object->m_destroyRequested || !object->m_guid.isValid())
                return false;
            const auto found = source.m_guidIndex.find(object->m_guid);
            if (found == source.m_guidIndex.end() || found->second != object.get())
                return false;
        }

        if (!reserveUpdateBuffers(source.m_objects.size()))
            return false;

        clear();
        m_objects.swap(source.m_objects);
        m_guidIndex.swap(source.m_guidIndex);
        for (const std::unique_ptr<GameObject>& object : m_objects)
            object->m_manager = this;
        return true;
    }

    bool GameObjectManager::reserveUpdateBuffers(const std::size_t capacity) noexcept
    {
        try
        {
            m_animatorsToUpdate.reserve(capacity);
            m_modelRenderersToSubmit.reserve(capacity);
            m_transformRootsToUpdate.reserve(capacity);
            m_lifecycleRoots.reserve(capacity);
            m_deactivationRoots.reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to reserve GameObjectManager update buffers.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("GameObjectManager update buffers exceed their maximum capacity.");
            return false;
        }
        return true;
    }

    bool GameObjectManager::collectLifecycleRoots(std::vector<GameObject*>& roots) noexcept
    {
        roots.clear();
        try
        {
            for (const std::unique_ptr<GameObject>& object : m_objects)
            {
                if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                    roots.push_back(object.get());
            }
        }
        catch (const std::bad_alloc&)
        {
            roots.clear();
            LOG_ERROR("Failed to allocate a GameObject lifecycle snapshot.");
            return false;
        }
        catch (const std::length_error&)
        {
            roots.clear();
            LOG_ERROR("GameObject lifecycle snapshot exceeded its maximum capacity.");
            return false;
        }
        return true;
    }

    void GameObjectManager::update(const float deltaTime) noexcept
    {
        if (m_isUpdatingLifecycle || m_isClearing)
        {
            LOG_ERROR("Cannot re-enter GameObjectManager lifecycle update.");
            return;
        }
        const ScopedFlag updateScope(m_isUpdatingLifecycle);
        if (!collectLifecycleRoots(m_lifecycleRoots))
            return;
        for (std::size_t index = 0; index < m_lifecycleRoots.size(); ++index)
        {
            GameObject* const object = m_lifecycleRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                object->initializeLifecycle();
        }

        if (!collectLifecycleRoots(m_lifecycleRoots))
            return;
        for (std::size_t index = 0; index < m_lifecycleRoots.size(); ++index)
        {
            GameObject* const object = m_lifecycleRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                object->startLifecycle();
        }

        if (!collectLifecycleRoots(m_lifecycleRoots))
            return;
        for (std::size_t index = 0; index < m_lifecycleRoots.size(); ++index)
        {
            GameObject* const object = m_lifecycleRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                object->updateLifecycle(deltaTime);
        }

        m_animatorsToUpdate.clear();
        for (const auto& object : m_objects)
        {
            AnimatorComponent* const animator = object->getComponent<AnimatorComponent>();
            if (animator != nullptr && animator->m_lifecycleEnabled
                && animator->m_lifecycleActive && animator->m_started)
                m_animatorsToUpdate.push_back(animator);
        }

        if (m_animatorsToUpdate.size() == 1)
        {
            m_animatorsToUpdate.front()->evaluatePendingUpdate(deltaTime);
        }
        else if (m_animatorsToUpdate.size() > 1)
        {
            JobSystem::instance().parallelFor(m_animatorsToUpdate.size(),
                [this, deltaTime](const std::size_t index)
                {
                    m_animatorsToUpdate[index]->evaluatePendingUpdate(deltaTime);
                });
        }
        m_animatorsToUpdate.clear();
    }

    void GameObjectManager::fixedUpdate(const float fixedDeltaTime) noexcept
    {
        if (m_isUpdatingLifecycle || m_isClearing)
        {
            LOG_ERROR("Cannot re-enter GameObjectManager lifecycle update.");
            return;
        }
        const ScopedFlag updateScope(m_isUpdatingLifecycle);
        if (!collectLifecycleRoots(m_lifecycleRoots))
            return;
        for (std::size_t index = 0; index < m_lifecycleRoots.size(); ++index)
        {
            GameObject* const object = m_lifecycleRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                object->fixedUpdateLifecycle(fixedDeltaTime);
        }
    }

    void GameObjectManager::lateUpdate(const float deltaTime) noexcept
    {
        if (m_isUpdatingLifecycle || m_isClearing)
        {
            LOG_ERROR("Cannot re-enter GameObjectManager lifecycle update.");
            return;
        }
        const ScopedFlag updateScope(m_isUpdatingLifecycle);
        m_transformRootsToUpdate.clear();
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                m_transformRootsToUpdate.push_back(object.get());

        if (m_transformRootsToUpdate.size() == 1)
        {
            m_transformRootsToUpdate.front()->updateWorldTransformHierarchy();
        }
        else if (m_transformRootsToUpdate.size() > 1)
        {
            JobSystem::instance().parallelFor(m_transformRootsToUpdate.size(),
                [this](const std::size_t index)
                {
                    m_transformRootsToUpdate[index]->updateWorldTransformHierarchy();
                });
        }
        m_transformRootsToUpdate.clear();

        if (!collectLifecycleRoots(m_lifecycleRoots))
            return;
        for (std::size_t index = 0; index < m_lifecycleRoots.size(); ++index)
        {
            GameObject* const object = m_lifecycleRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr && !object->m_destroyRequested)
                object->lateUpdateLifecycle(deltaTime);
        }

        m_modelRenderersToSubmit.clear();
        for (const auto& object : m_objects)
        {
            ModelRendererComponent* const renderer = object->getComponent<ModelRendererComponent>();
            if (renderer != nullptr && renderer->m_lifecycleEnabled
                && renderer->m_lifecycleActive && renderer->m_started
                && renderer->m_submissionPending)
                m_modelRenderersToSubmit.push_back(renderer);
        }

        if (m_modelRenderersToSubmit.size() == 1)
        {
            m_modelRenderersToSubmit.front()->submitPendingRender();
        }
        else if (m_modelRenderersToSubmit.size() > 1)
        {
            JobSystem::instance().parallelFor(m_modelRenderersToSubmit.size(),
                [this](const std::size_t index)
                {
                    m_modelRenderersToSubmit[index]->submitPendingRender();
                });
        }
        m_modelRenderersToSubmit.clear();
    }

    void GameObjectManager::processDestroyQueue() noexcept
    {
        if (m_isUpdatingLifecycle || m_isDeactivatingLifecycle || m_isProcessingDestroyQueue || m_isClearing)
        {
            LOG_ERROR("Cannot process GameObject destruction during lifecycle or destruction callbacks.");
            return;
        }
        const ScopedFlag destroyScope(m_isProcessingDestroyQueue);
        std::vector<GameObject*> pending;
        try
        {
            pending.reserve(m_objects.size());
            pending.insert(pending.end(), m_destroyQueue.begin(), m_destroyQueue.end());
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while preparing the GameObject destruction queue.");
            return;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("GameObject collection is too large to process its destruction queue.");
            return;
        }
        m_destroyQueue.clear();
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            GameObject* object = pending[index];
            if (object == nullptr || object->m_manager != this)
                continue;
            for (GameObject* child : object->m_children)
            {
                if (std::find(pending.begin(), pending.end(), child) == pending.end())
                    pending.push_back(child);
            }
        }

        for (GameObject* object : pending)
        {
            if (object != nullptr && object->m_manager == this)
                object->m_destroyRequested = true;
        }

        for (GameObject* object : pending)
        {
            if (object == nullptr || object->m_manager != this)
                continue;
            object->shutdownLifecycle();
            m_guidIndex.erase(object->m_guid);
            const auto found = std::find_if(m_objects.begin(), m_objects.end(),
                [object](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == object; });
            if (found != m_objects.end())
                m_objects.erase(found);
        }
    }

    void GameObjectManager::clear() noexcept
    {
        if (m_isClearing)
            return;
        if (m_isUpdatingLifecycle || m_isDeactivatingLifecycle || m_isProcessingDestroyQueue)
        {
            LOG_ERROR("Cannot clear GameObjects while a lifecycle or destruction callback is active.");
            return;
        }
        const ScopedFlag clearScope(m_isClearing);
        for (const auto& object : m_objects)
            object->shutdownLifecycle();
        m_destroyQueue.clear();
        m_guidIndex.clear();
        m_objects.clear();
        m_lifecycleRoots.clear();
        m_deactivationRoots.clear();
        m_transformRootsToUpdate.clear();
    }

    void GameObjectManager::deactivateLifecycle() noexcept
    {
        if (m_isClearing || m_isDeactivatingLifecycle)
        {
            LOG_ERROR("Cannot re-enter lifecycle deactivation or deactivate while the manager is clearing.");
            return;
        }
        const ScopedFlag deactivationScope(m_isDeactivatingLifecycle);
        if (!collectLifecycleRoots(m_deactivationRoots))
            return;
        for (std::size_t index = 0; index < m_deactivationRoots.size(); ++index)
        {
            GameObject* const object = m_deactivationRoots[index];
            if (object->m_manager == this && object->m_parent == nullptr)
                object->deactivateLifecycle();
        }
    }

    GameObject* GameObjectManager::find(const std::string& name) noexcept
    {
        const auto found = std::find_if(m_objects.begin(), m_objects.end(),
            [&name](const std::unique_ptr<GameObject>& object) {
                return object->getParent() == nullptr && object->getName() == name;
            });
        if (found != m_objects.end())
            return found->get();
        for (const auto& object : m_objects)
        {
            if (object->getParent() == nullptr)
            {
                if (GameObject* result = object->find(name))
                    return result;
            }
        }
        return nullptr;
    }

    const GameObject* GameObjectManager::find(const std::string& name) const noexcept
    {
        return const_cast<GameObjectManager*>(this)->find(name);
    }

    GameObject* GameObjectManager::find(const ObjectGUID& guid) noexcept
    {
        const auto found = m_guidIndex.find(guid);
        return found == m_guidIndex.end() ? nullptr : found->second;
    }

    const GameObject* GameObjectManager::find(const ObjectGUID& guid) const noexcept
    {
        const auto found = m_guidIndex.find(guid);
        return found == m_guidIndex.end() ? nullptr : found->second;
    }

    GameObject* GameObjectManager::findWithTag(const TagID tag) noexcept
    {
        const auto found = std::find_if(m_objects.begin(), m_objects.end(),
            [tag](const std::unique_ptr<GameObject>& object) { return object->getTag() == tag; });
        return found == m_objects.end() ? nullptr : found->get();
    }

    std::vector<GameObject*> GameObjectManager::findGameObjectsWithTag(const TagID tag) noexcept
    {
        std::vector<GameObject*> result;
        try
        {
            result.reserve(m_objects.size());
            for (const auto& object : m_objects)
            {
                if (object->getTag() == tag)
                    result.push_back(object.get());
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while searching GameObjects by tag.");
            return {};
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("GameObject collection is too large to search by tag.");
            return {};
        }
        return result;
    }

    std::vector<GameObject*> GameObjectManager::findGameObjectsInLayer(const LayerID layer) noexcept
    {
        std::vector<GameObject*> result;
        try
        {
            result.reserve(m_objects.size());
            for (const auto& object : m_objects)
            {
                if (object->getLayer() == layer)
                    result.push_back(object.get());
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while searching GameObjects by layer.");
            return {};
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("GameObject collection is too large to search by layer.");
            return {};
        }
        return result;
    }
} // namespace Engine