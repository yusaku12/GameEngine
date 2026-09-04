#pragma once

#include <vector>
#include <deque>

#include "Core\ECS\ECSWorld.h"
#include "Core\GameObject\GameObject.h"

namespace Engine
{
    /**
     * @brief GameObjectの生成・破棄・参照を管理するマネージャ。
     */
    class GameObjectManager
    {
    public:
        GameObjectManager();
        ~GameObjectManager();

        GE_DISABLE_COPY_AND_MOVE(GameObjectManager);

        [[nodiscard]] GameObjectHandle Create(const std::string& name = "GameObject");
        void Destroy(GameObjectHandle handle);

        [[nodiscard]] GameObject* Get(GameObjectHandle handle) noexcept;
        [[nodiscard]] const GameObject* Get(GameObjectHandle handle) const noexcept;
        [[nodiscard]] bool IsValid(GameObjectHandle handle) const noexcept;

        void SetParent(GameObjectHandle child, GameObjectHandle parent);

        void Update(float deltaTime);
        void FixedUpdate(float fixedDeltaTime);
        void LateUpdate(float deltaTime);
        void Clear();

    private:
        struct Slot
        {
            std::unique_ptr<GameObject> object;
            bool active = false;
        };

        std::vector<Slot> m_slots;
        std::deque<GameObjectHandle> m_destroyQueue;
        ECSWorld m_world;
        std::uint32_t m_nextIndex = 0;

        [[nodiscard]] GameObjectHandle AllocateHandle() noexcept;
        void ProcessDestroyQueue();
        void SyncTransformsToWorld();
        void SyncTransformsFromWorld();
    };
} // namespace Engine
