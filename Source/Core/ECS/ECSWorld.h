#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Core\GameObject\Component.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    using Entity = entt::entity;

    struct TransformComponent : public Component
    {
        Transform transform;

        TransformComponent() = default;
        explicit TransformComponent(const Transform& value) : transform(value) {}
    };

    struct VelocityComponent : public Component
    {
        Vector3 velocity = Vector3::Zero;

        VelocityComponent() = default;
        explicit VelocityComponent(const Vector3& value) : velocity(value) {}
    };

    class ECSWorld;

    class ECSSystem
    {
    public:
        virtual ~ECSSystem() = default;
        virtual void Update(ECSWorld& world, float deltaTime) = 0;
    };

    class TransformSystem : public ECSSystem
    {
    public:
        void Update(ECSWorld& world, float) override;
    };

    class MovementSystem : public ECSSystem
    {
    public:
        void Update(ECSWorld& world, float deltaTime) override;
    };

    /**
     * @brief EnTTを利用したECSのラッパー。
     */
    class ECSWorld
    {
    public:
        ECSWorld() = default;
        ~ECSWorld() = default;

        [[nodiscard]] Entity CreateEntity();
        void DestroyEntity(Entity entity);
        [[nodiscard]] bool IsValid(Entity entity) const noexcept;

        template <typename T, typename... Args>
        T& AddComponent(Entity entity, Args&&... args)
        {
            return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
        }

        template <typename T>
        T& GetComponent(Entity entity)
        {
            return m_registry.get<T>(entity);
        }

        template <typename T>
        [[nodiscard]] bool HasComponent(Entity entity) const
        {
            return m_registry.any_of<T>(entity);
        }

        template <typename T>
        void RemoveComponent(Entity entity)
        {
            m_registry.remove<T>(entity);
        }

        template <typename T, typename... Args>
        T& AddSystem(Args&&... args)
        {
            auto system = std::make_unique<T>(std::forward<Args>(args)...);
            T& result = *system;
            m_systems.push_back(std::move(system));
            return result;
        }

        void Update(float deltaTime)
        {
            for (auto& system : m_systems)
            {
                if (system)
                    system->Update(*this, deltaTime);
            }
        }

        void Clear();

        [[nodiscard]] entt::registry& Registry() noexcept { return m_registry; }
        [[nodiscard]] const entt::registry& Registry() const noexcept { return m_registry; }

    private:
        entt::registry m_registry;
        std::vector<std::unique_ptr<ECSSystem>> m_systems;
    };

    inline void TransformSystem::Update(ECSWorld& world, float)
    {
        auto view = world.Registry().view<TransformComponent>();
        for (auto entity : view)
        {
            auto& component = view.get<TransformComponent>(entity);
            (void)component;
        }
    }

    inline void MovementSystem::Update(ECSWorld& world, float deltaTime)
    {
        auto view = world.Registry().view<TransformComponent, VelocityComponent>();
        for (auto entity : view)
        {
            auto& transformComponent = view.get<TransformComponent>(entity);
            auto& velocityComponent = view.get<VelocityComponent>(entity);
            transformComponent.transform.translate(velocityComponent.velocity * deltaTime);
        }
    }
} // namespace Engine
