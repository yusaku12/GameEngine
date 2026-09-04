#pragma once

#include <cstdint>
#include <string>
#include "Pch.h"

namespace Engine
{
    class GameObject;

    using ComponentTypeID = std::uint32_t;

    template <typename T>
    ComponentTypeID GetComponentTypeID() noexcept
    {
        static const ComponentTypeID id = []() {
            static ComponentTypeID nextId = 0;
            const ComponentTypeID value = nextId++;
            return value;
        }();
        return id;
    }

    /**
     * @brief GameObjectにアタッチされる基本コンポーネント。
     */
    class Component
    {
    public:
        virtual ~Component() = default;

        virtual void Initialize() {}
        virtual void Start() {}
        virtual void Update(float) {}
        virtual void FixedUpdate(float) {}
        virtual void LateUpdate(float) {}
        virtual void OnDestroy() {}

        [[nodiscard]] GameObject& GetGameObject() noexcept;
        [[nodiscard]] const GameObject& GetGameObject() const noexcept;

    protected:
        friend class GameObject;
        GameObject* m_gameObject = nullptr;
    };
} // namespace Engine
