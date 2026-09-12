#pragma once

#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"

namespace Engine
{
    using ComponentTypeID = std::uint32_t;

    /**
     * @brief Component型のEditor/runtime向けメタデータ。
     */
    struct ComponentTypeInfo
    {
        ComponentTypeID id = 0;        //!< Component型ID。ComponentRegistryが自動で割り当てる。
        std::string_view name;         //!< Component型の表示名。Editorで使用される。
        bool allowMultiple = false;    //!< 同一GameObjectへの複数追加を許可するか
        bool required = false;         //!< 必須Componentか
        bool executeLifecycle = false; //!< Lifecycleを実行するか
    };

    /**
     * @brief Component型とメタデータの対応を管理するRegistry。
     * @thread_safety Main thread only.
     */
    class ComponentRegistry
    {
    public:

        /** @brief ComponentRegistryのシングルトンを取得する。 */
        static ComponentRegistry& instance() noexcept;

        ComponentRegistry() = default;
        GE_DISABLE_COPY_AND_MOVE(ComponentRegistry);

        /**
         * @brief Component型をメタデータ付きで登録する。
         * @tparam T 登録するComponent型
         * @param name 表示名
         * @param allowMultiple 同一GameObjectへの複数追加を許可するか
         * @param required 必須Componentか
         * @param executeLifecycle Lifecycleを実行するか
         * @return 登録されたComponent型ID
         */
        template <typename T>
        ComponentTypeID registerType(std::string_view name, const bool allowMultiple = false,
            const bool required = false, const bool executeLifecycle = false)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const std::type_index type = typeid(T);
            if (const auto found = m_types.find(type); found != m_types.end())
                return found->second.id;

            const ComponentTypeInfo info{
                .id = m_nextId++,
                .name = name,
                .allowMultiple = allowMultiple,
                .required = required,
                .executeLifecycle = executeLifecycle
            };
            m_types.emplace(type, info);
            m_ids.emplace(info.id, type);
            return info.id;
        }

        /**
         * @brief 未登録のComponent型を既定メタデータで登録する。
         * @tparam T 登録するComponent型
         * @return Component型ID
         */
        template <typename T>
        ComponentTypeID ensureRegistered()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            if (const auto found = m_types.find(std::type_index(typeid(T))); found != m_types.end())
                return found->second.id;
            return registerType<T>(typeid(T).name());
        }

        /**
         * @brief 型からComponentメタデータを取得する。
         * @tparam T 取得するComponent型
         * @return 登録済みメタデータ。未登録の場合はnullptr
         */
        template <typename T>
        const ComponentTypeInfo* get() const noexcept
        {
            const auto found = m_types.find(std::type_index(typeid(T)));
            return found == m_types.end() ? nullptr : &found->second;
        }

        /**
         * @brief Component型IDからメタデータを取得する。.
         * @param id 取得するComponent型ID
         */
        const ComponentTypeInfo* get(ComponentTypeID id) const noexcept;

        /**
         * @brief std::type_indexからメタデータを取得する。.
         * @param type 取得するComponent型のtype_index
         * @return 登録済みメタデータ。未登録の場合はnullptr
         */
        const ComponentTypeInfo* get(std::type_index type) const noexcept;

        /**
         * @brief 表示名からComponentメタデータを検索する。.
         * @param name 取得するComponent型の表示名
         * @return 登録済みメタデータ。未登録の場合はnullptr
         */
        const ComponentTypeInfo* findByName(std::string_view name) const noexcept;

        /**
         * @brief 登録済みComponent型をすべて削除する。.
         */
        void clear() noexcept;

    private:

        ComponentTypeID m_nextId = 1;                                   //!< 次に割り当てるComponent型ID。0は無効IDとして予約する。
        std::unordered_map<std::type_index, ComponentTypeInfo> m_types; //!< Component型とメタデータの対応
        std::unordered_map<ComponentTypeID, std::type_index> m_ids;     //!< Component型IDとComponent型の対応
    };
} // namespace Engine