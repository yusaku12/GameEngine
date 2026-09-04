#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace entt
{
    using entity = std::uint32_t;
    inline constexpr entity null = static_cast<entity>(-1);

    namespace detail
    {
        struct entity_hash
        {
            std::size_t operator()(entity value) const noexcept
            {
                return static_cast<std::size_t>(value);
            }
        };

        struct storage_base
        {
            virtual ~storage_base() = default;
            virtual void erase(entity value) = 0;
            virtual bool contains(entity value) const = 0;
            virtual void clear() = 0;
        };

        template <typename T>
        struct storage : storage_base
        {
            std::unordered_map<entity, T, entity_hash> values;

            void erase(entity value) override
            {
                values.erase(value);
            }

            bool contains(entity value) const override
            {
                return values.find(value) != values.end();
            }

            void clear() override
            {
                values.clear();
            }
        };
    }

    template <typename Type>
    using storage_type = std::unordered_map<entity, Type, detail::entity_hash>;

    class registry
    {
    public:
        registry() = default;
        ~registry() = default;

        entity create()
        {
            const entity id = static_cast<entity>(m_nextId++);
            m_entities.push_back(id);
            m_alive.insert(id);
            return id;
        }

        void destroy(entity value)
        {
            for (auto& [typeId, storage] : m_components)
                storage->erase(value);
            m_alive.erase(value);
        }

        bool valid(entity value) const noexcept
        {
            return m_alive.find(value) != m_alive.end();
        }

        void clear()
        {
            for (auto& [typeId, storage] : m_components)
                storage->clear();
            m_components.clear();
            m_alive.clear();
            m_entities.clear();
            m_nextId = 0;
        }

        template <typename T, typename... Args>
        T& emplace(entity value, Args&&... args)
        {
            auto& storage = get_storage<T>();
            auto [it, inserted] = storage.values.emplace(value, T(std::forward<Args>(args)...));
            (void)inserted;
            m_alive.insert(value);
            return it->second;
        }

        template <typename T>
        T& get(entity value)
        {
            auto& storage = get_storage<T>();
            auto it = storage.values.find(value);
            if (it == storage.values.end())
                throw std::out_of_range("entt::registry::get: component not found");
            return it->second;
        }

        template <typename T>
        const T& get(entity value) const
        {
            const auto& storage = get_storage<T>();
            auto it = storage.values.find(value);
            if (it == storage.values.end())
                throw std::out_of_range("entt::registry::get: component not found");
            return it->second;
        }

        template <typename T>
        bool any_of(entity value) const
        {
            const auto it = m_components.find(std::type_index(typeid(T)));
            if (it == m_components.end())
                return false;
            return it->second->contains(value);
        }

        template <typename T>
        void remove(entity value)
        {
            auto it = m_components.find(std::type_index(typeid(T)));
            if (it == m_components.end())
                return;
            it->second->erase(value);
        }

        template <typename... Components>
        class basic_view
        {
        public:
            explicit basic_view(registry& owner) : m_owner(owner) { build(); }

            using iterator = std::vector<entity>::iterator;
            iterator begin() { return m_entities.begin(); }
            iterator end() { return m_entities.end(); }

            template <typename T>
            T& get(entity value)
            {
                return m_owner.get<T>(value);
            }

            bool empty() const { return m_entities.empty(); }

        private:
            void build()
            {
                m_entities.clear();
                for (entity value : m_owner.m_entities)
                {
                    if (m_owner.valid(value) && (m_owner.any_of<Components>(value) && ...))
                        m_entities.push_back(value);
                }
            }

            registry& m_owner;
            std::vector<entity> m_entities;
        };

        template <typename... Components>
        basic_view<Components...> view()
        {
            return basic_view<Components...>(*this);
        }

    private:
        template <typename T>
        detail::storage<T>& get_storage()
        {
            const auto typeId = std::type_index(typeid(T));
            auto it = m_components.find(typeId);
            if (it == m_components.end())
            {
                auto storage = std::make_unique<detail::storage<T>>();
                auto result = m_components.emplace(typeId, std::move(storage));
                it = result.first;
            }
            return *static_cast<detail::storage<T>*>(it->second.get());
        }

        template <typename T>
        const detail::storage<T>& get_storage() const
        {
            const auto typeId = std::type_index(typeid(T));
            auto it = m_components.find(typeId);
            if (it == m_components.end())
                throw std::out_of_range("entt::registry::get_storage: storage not found");
            return *static_cast<const detail::storage<T>*>(it->second.get());
        }

        std::unordered_map<std::type_index, std::unique_ptr<detail::storage_base>, std::hash<std::type_index>> m_components;
        std::unordered_set<entity, detail::entity_hash> m_alive;
        std::vector<entity> m_entities;
        entity m_nextId = 0;
    };
} // namespace entt
