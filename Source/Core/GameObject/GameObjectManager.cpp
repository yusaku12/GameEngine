#include "Pch.h"

#include "Core\GameObject\GameObjectManager.h"

namespace Engine
{
    GameObjectManager::GameObjectManager()
    {
        m_world.AddSystem<TransformSystem>();
        m_world.AddSystem<MovementSystem>();
    }

    GameObjectManager::~GameObjectManager()
    {
        Clear();
    }

    GameObjectHandle GameObjectManager::Create(const std::string& name)
    {
        GameObjectHandle handle = AllocateHandle();
        if (m_slots.size() <= handle.index)
            m_slots.resize(static_cast<size_t>(handle.index) + 1);

        const Entity entity = m_world.CreateEntity();
        auto object = std::make_unique<GameObject>(name, &m_world, entity);
        object->SetHandle(handle);
        object->AddComponent<TransformComponent>(object->GetTransform());
        m_slots[handle.index].object = std::move(object);
        m_slots[handle.index].active = true;
        return handle;
    }

    void GameObjectManager::Destroy(GameObjectHandle handle)
    {
        if (!IsValid(handle))
            return;

        m_destroyQueue.push_back(handle);
    }

    GameObject* GameObjectManager::Get(GameObjectHandle handle) noexcept
    {
        if (!IsValid(handle))
            return nullptr;

        const auto index = static_cast<size_t>(handle.index);
        if (index >= m_slots.size() || !m_slots[index].object)
            return nullptr;
        return m_slots[index].object.get();
    }

    const GameObject* GameObjectManager::Get(GameObjectHandle handle) const noexcept
    {
        if (!IsValid(handle))
            return nullptr;

        const auto index = static_cast<size_t>(handle.index);
        if (index >= m_slots.size() || !m_slots[index].object)
            return nullptr;
        return m_slots[index].object.get();
    }

    bool GameObjectManager::IsValid(GameObjectHandle handle) const noexcept
    {
        const auto index = static_cast<size_t>(handle.index);
        if (index >= m_slots.size())
            return false;
        if (!m_slots[index].object)
            return false;
        return m_slots[index].object->GetHandle().generation == handle.generation;
    }

    void GameObjectManager::SetParent(GameObjectHandle child, GameObjectHandle parent)
    {
        if (!IsValid(child) || child == parent)
            return;

        auto* childObject = Get(child);
        if (childObject == nullptr)
            return;

        if (IsValid(parent))
        {
            auto* parentObject = Get(parent);
            if (parentObject == nullptr)
                return;

            for (const GameObject* ancestor = parentObject; ancestor != nullptr; ancestor = ancestor->m_parent)
            {
                if (ancestor->GetHandle() == child)
                    return;
            }
        }

        if (childObject->m_parent != nullptr && childObject->m_parentHandle.IsValid())
        {
            auto* oldParent = childObject->m_parent;
            if (oldParent != nullptr)
                oldParent->RemoveChild(child);
        }

        childObject->m_parent = nullptr;
        childObject->m_parentHandle = {};

        if (!IsValid(parent))
        {
            return;
        }

        auto* parentObject = Get(parent);
        childObject->m_parent = parentObject;
        childObject->m_parentHandle = parent;
        parentObject->AddChild(child);
    }

    void GameObjectManager::Update(float deltaTime)
    {
        ProcessDestroyQueue();
        SyncTransformsToWorld();
        m_world.Update(deltaTime);
        SyncTransformsFromWorld();

        for (auto& slot : m_slots)
        {
            if (slot.object && slot.active && !slot.object->IsDestroyed())
                slot.object->Update(deltaTime);
        }
    }

    void GameObjectManager::FixedUpdate(float fixedDeltaTime)
    {
        for (auto& slot : m_slots)
        {
            if (slot.object && slot.active && !slot.object->IsDestroyed())
                slot.object->FixedUpdate(fixedDeltaTime);
        }
    }

    void GameObjectManager::LateUpdate(float deltaTime)
    {
        for (auto& slot : m_slots)
        {
            if (slot.object && slot.active && !slot.object->IsDestroyed())
                slot.object->LateUpdate(deltaTime);
        }
    }

    void GameObjectManager::Clear()
    {
        m_world.Clear();
        for (auto& slot : m_slots)
        {
            if (slot.object)
                slot.object->Destroy();
            slot.object.reset();
            slot.active = false;
        }

        m_slots.clear();
        m_destroyQueue.clear();
        m_nextIndex = 0;
    }

    GameObjectHandle GameObjectManager::AllocateHandle() noexcept
    {
        std::uint32_t index = m_nextIndex;
        if (m_slots.size() > index && m_slots[index].object)
        {
            index = static_cast<std::uint32_t>(m_slots.size());
            while (index < m_slots.size() && m_slots[index].object != nullptr)
                ++index;
        }

        while (m_slots.size() <= index)
            m_slots.emplace_back();

        GameObjectHandle handle{};
        handle.index = index;
        handle.generation = (m_slots[index].object == nullptr) ? 1u : (m_slots[index].object->GetHandle().generation + 1u);
        m_nextIndex = index + 1;
        return handle;
    }

    void GameObjectManager::ProcessDestroyQueue()
    {
        while (!m_destroyQueue.empty())
        {
            const GameObjectHandle handle = m_destroyQueue.front();
            m_destroyQueue.pop_front();
            if (!IsValid(handle))
                continue;

            const auto index = static_cast<size_t>(handle.index);
            if (index < m_slots.size() && m_slots[index].object)
            {
                auto* object = m_slots[index].object.get();

                if (object->m_parent != nullptr && object->m_parentHandle.IsValid())
                    object->m_parent->RemoveChild(handle);

                for (const GameObjectHandle& childHandle : object->m_children)
                {
                    if (!IsValid(childHandle))
                        continue;

                    auto* childObject = Get(childHandle);
                    if (childObject != nullptr)
                    {
                        childObject->m_parent = nullptr;
                        childObject->m_parentHandle = {};
                    }
                }

                object->m_children.clear();
                object->Destroy();
                m_slots[index].object.reset();
                m_slots[index].active = false;
            }
        }
    }

    void GameObjectManager::SyncTransformsToWorld()
    {
        for (auto& slot : m_slots)
        {
            if (!slot.object || !slot.active || slot.object->IsDestroyed())
                continue;

            const auto entity = slot.object->m_entity;
            if (!m_world.IsValid(entity))
                continue;

            if (m_world.HasComponent<TransformComponent>(entity))
                m_world.GetComponent<TransformComponent>(entity).transform = slot.object->GetTransform();
        }
    }

    void GameObjectManager::SyncTransformsFromWorld()
    {
        for (auto& slot : m_slots)
        {
            if (!slot.object || !slot.active || slot.object->IsDestroyed())
                continue;

            const auto entity = slot.object->m_entity;
            if (!m_world.IsValid(entity))
                continue;

            if (m_world.HasComponent<TransformComponent>(entity))
                slot.object->m_transform = m_world.GetComponent<TransformComponent>(entity).transform;
        }
    }
} // namespace Engine
