#pragma once

#include <type_traits>

#include "Core\Math\Transform.h"

namespace Engine
{
    class GameObject;

    /**
     * @brief GameObjectが所有する機能拡張の基底クラス。
     * @thread_safety Main thread only.
     */
    class Component
    {
    public:
        virtual ~Component() = default;

        [[nodiscard]] GameObject* getGameObject() noexcept { return m_gameObject; }
        [[nodiscard]] const GameObject* getGameObject() const noexcept { return m_gameObject; }
        [[nodiscard]] Transform* getTransform() noexcept;
        [[nodiscard]] const Transform* getTransform() const noexcept;

        [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }
        void setEnabled(bool enabled) noexcept;

    protected:
        friend class GameObject;
        void setGameObject(GameObject* gameObject) noexcept { m_gameObject = gameObject; }

        virtual void onAwake() {}
        virtual void onEnable() {}
        virtual void onStart() {}
        virtual void onUpdate(float) {}
        virtual void onFixedUpdate(float) {}
        virtual void onLateUpdate(float) {}
        virtual void onDisable() {}
        virtual void onDestroy() {}

        GameObject* m_gameObject = nullptr;
        bool m_enabled = true;

    private:
        friend class GameObjectManager;
        void setLifecycleEnabled(bool enabled) noexcept { m_lifecycleEnabled = enabled; }
        void invokeAwake() noexcept;
        void invokeEnable() noexcept;
        void invokeStart() noexcept;
        void invokeUpdate(float deltaTime) noexcept;
        void invokeFixedUpdate(float fixedDeltaTime) noexcept;
        void invokeLateUpdate(float deltaTime) noexcept;
        void invokeDisable() noexcept;
        void invokeDestroy() noexcept;

        bool m_lifecycleEnabled = false;
        bool m_awakened = false;
        bool m_started = false;
        bool m_lifecycleActive = false;
    };
} // namespace Engine