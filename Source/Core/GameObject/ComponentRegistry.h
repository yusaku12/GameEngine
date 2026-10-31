#pragma once

#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"
#include <vector>

namespace Engine
{
    using ComponentTypeID = std::uint32_t;

    /**
     * @brief Component型のEditor/runtime向けメタデータ。
     */
    struct ComponentTypeInfo
    {
        using Factory = std::unique_ptr<Component>(*)();
        using EditorAdditionRule = std::string_view(*)(const GameObject&);

        ComponentTypeID id = 0;        //!< Component型ID。ComponentRegistryが自動で割り当てる。
        std::string name;              //!< Component型の表示名。Editorで使用される。
        Factory factory = nullptr;     //!< Component生成関数。
        bool allowMultiple = false;    //!< 同一GameObjectへの複数追加を許可するか
        bool required = false;         //!< 必須Componentか
        bool executeLifecycle = false; //!< Lifecycleを実行するか
        std::type_index type = typeid(void); //!< 登録されたComponentの型。
        EditorAdditionRule editorAdditionRule = nullptr; //!< 追加不可の理由を返す。文字列は呼び出し後も有効であること。

        /**
         * @brief エディターで指定オブジェクトに追加できない理由を取得する。
         * @param object 追加先のGameObject。
         * @return 追加可能な場合は空文字列。それ以外は無効化の理由。
         */
        std::string_view getEditorAdditionBlockReason(const GameObject& object) const;
    };

    /**
     * @brief Component型とメタデータの対応を管理するRegistry。
     * @thread_safety 外部で同期されたエンジンスレッドから使用すること。同時アクセスは不可。
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
         * @param editorAdditionRule エディターの追加条件。省略時は重複と必須Componentのみを判定する。
         * @return 登録されたComponent型ID
         */
        template <typename T>
        ComponentTypeID registerType(std::string_view name, const bool allowMultiple = false,
            const bool required = false, const bool executeLifecycle = false,
            ComponentTypeInfo::EditorAdditionRule editorAdditionRule = nullptr)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const std::type_index type = typeid(T);
            return registerTypeInfo(type, name,
                []() -> std::unique_ptr<Component> { return std::make_unique<T>(); },
                allowMultiple, required, executeLifecycle, editorAdditionRule);
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

        /** @brief 登録名からComponentを生成する。未知型の場合はnullptr。 */
        std::unique_ptr<Component> create(std::string_view name) const;

        /**
         * @brief 登録済みComponentのスナップショットを表示名順で取得する。
         * @param types 取得先。失敗時は空になる。
         * @return 取得に成功した場合はtrue。
         */
        bool getRegisteredTypes(std::vector<ComponentTypeInfo>& types) const noexcept;

        /**
         * @brief 登録済みComponent型をすべて削除する。.
         */
        void clear() noexcept;

    private:

        /**
         * @brief Component型をメタデータ付きで登録する。
         * @param type 登録するComponent型のtype_index
         * @param name 表示名
         * @param factory Component生成関数
         * @param allowMultiple 同一GameObjectへの複数追加を許可するか
         * @param required 必須Componentか
         * @param executeLifecycle Lifecycleを実行するか
         * @param editorAdditionRule エディターの追加条件。
         * @return 登録されたComponent型ID
         */
        ComponentTypeID registerTypeInfo(std::type_index type, std::string_view name,
            ComponentTypeInfo::Factory factory, bool allowMultiple, bool required, bool executeLifecycle,
            ComponentTypeInfo::EditorAdditionRule editorAdditionRule);

        ComponentTypeID m_nextId = 1;                                   //!< 次に割り当てるComponent型ID。0は無効IDとして予約する。
        std::unordered_map<std::type_index, ComponentTypeInfo> m_types; //!< Component型とメタデータの対応
        std::unordered_map<ComponentTypeID, std::type_index> m_ids;     //!< Component型IDとComponent型の対応
    };
} // namespace Engine