#include "Pch.h"
#include "Core\GameObject\GameObjectManager.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\Threading\JobSystem.h"

namespace Engine
{
    GameObject* GameObjectManager::create(const std::string& name)
    {
        return create(name, ObjectGUID::generate());
    }

    GameObject* GameObjectManager::create(const std::string& name, const ObjectGUID guid)
    {
        if (!guid.isValid() || m_guidIndex.contains(guid))
            return nullptr;

        auto object = std::unique_ptr<GameObject>(new GameObject(*this, guid, name));
        GameObject* result = object.get();
        m_guidIndex.emplace(guid, result);
        m_objects.push_back(std::move(object));
        return result;
    }

    void GameObjectManager::destroy(GameObject* object) noexcept
    {
        if (object == nullptr || object->m_manager != this)
            return;
        if (std::find(m_destroyQueue.begin(), m_destroyQueue.end(), object) == m_destroyQueue.end())
            m_destroyQueue.push_back(object);
    }

    bool GameObjectManager::transferTo(GameObject* object, GameObjectManager& destination) noexcept
    {
        if (object == nullptr || object->m_manager != this || object->m_parent != nullptr
            || &destination == this || object->m_destroyRequested)
            return false;

        std::vector<GameObject*> subtree;
        const auto collect = [&subtree](GameObject* current, const auto& collectSelf) -> void
            {
                subtree.push_back(current);
                for (GameObject* child : current->m_children)
                    collectSelf(child, collectSelf);
            };
        collect(object, collect);

        for (GameObject* current : subtree)
        {
            const auto found = std::find_if(m_objects.begin(), m_objects.end(),
                [current](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == current; });
            if (found == m_objects.end() || destination.m_guidIndex.contains(current->m_guid))
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
            destination.m_guidIndex.emplace(current->m_guid, current);
            destination.m_objects.push_back(std::move(ownership));
        }
        return true;
    }

    void GameObjectManager::update(const float deltaTime) noexcept
    {
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                object->initializeLifecycle();
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                object->startLifecycle();
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                object->updateLifecycle(deltaTime);

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
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                object->fixedUpdateLifecycle(fixedDeltaTime);
    }

    void GameObjectManager::lateUpdate(const float deltaTime) noexcept
    {
        for (const auto& object : m_objects)
            if (object->getParent() == nullptr)
                object->lateUpdateLifecycle(deltaTime);

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
        std::vector<GameObject*> pending = std::move(m_destroyQueue);
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
        for (const auto& object : m_objects)
            object->shutdownLifecycle();
        m_destroyQueue.clear();
        m_guidIndex.clear();
        m_objects.clear();
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
        const auto objects = findGameObjectsWithTag(tag);
        return objects.empty() ? nullptr : objects.front();
    }

    std::vector<GameObject*> GameObjectManager::findGameObjectsWithTag(const TagID tag) noexcept
    {
        std::vector<GameObject*> result;
        result.reserve(m_objects.size());
        for (const auto& object : m_objects)
        {
            if (object->getTag() == tag)
                result.push_back(object.get());
        }
        return result;
    }

    std::vector<GameObject*> GameObjectManager::findGameObjectsInLayer(const LayerID layer) noexcept
    {
        std::vector<GameObject*> result;
        result.reserve(m_objects.size());
        for (const auto& object : m_objects)
        {
            if (object->getLayer() == layer)
                result.push_back(object.get());
        }
        return result;
    }
} // namespace Engine