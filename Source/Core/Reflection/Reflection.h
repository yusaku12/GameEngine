#pragma once

namespace Engine
{
    /** @brief リフレクションで扱うプロパティの型。 */
    enum class PropertyType
    {
        Boolean,         //!< bool型
        Integer,         //!< 符号付き整数型
        UnsignedInteger, //!< 符号なし整数型
        Float,           //!< 浮動小数点型
        Vector3,         //!< Vector3型
        Quaternion,      //!< Quaternion型
        String           //!< 文字列型
    };

    /** @brief リフレクション対象プロパティのメタデータ。 */
    struct PropertyInfo
    {
        std::string_view name;                   //!< プロパティ名
        PropertyType type = PropertyType::Float; //!< プロパティの型
        std::size_t offset = 0;                  //!< 型先頭からのバイトオフセット
        std::size_t size = 0;                    //!< プロパティのバイトサイズ
        bool editable = false;                   //!< Editorで編集可能か
    };

    /** @brief 型名と所属プロパティのメタデータ。 */
    struct TypeInfo
    {
        std::string_view name;                //!< Editorで表示する型名
        std::vector<PropertyInfo> properties; //!< プロパティ一覧
    };

    /**
     * @brief Editor向け型・PropertyメタデータRegistry。
     * @thread_safety Main thread only.
     */
    class ReflectionRegistry
    {
    public:

        /** @brief リフレクションレジストリのシングルトンを取得する。 */
        static ReflectionRegistry& instance() noexcept;

        /**
         * @brief 型を登録し、空の型情報を返す。
         * @tparam T 登録するC++型
         * @param name エディタで表示する型名。参照先はRegistryより長く存続させること
         * @return 登録された型情報
         */
        template <typename T>
        TypeInfo& registerType(std::string_view name)
        {
            return m_types.insert_or_assign(
                std::type_index(typeid(T)), TypeInfo{ name, {} }).first->second;
        }

        /**
         * @brief 型情報を取得する。
         * @tparam T 取得するC++型
         * @return 登録済みの場合は型情報、未登録の場合は nullptr
         */
        template <typename T>
        TypeInfo* get() noexcept
        {
            const auto found = m_types.find(std::type_index(typeid(T)));
            return found == m_types.end() ? nullptr : &found->second;
        }

        /**
         * @brief std::type_indexから型情報を取得する。
         * @param type 取得する型のstd::type_index
         * @return 登録済みの場合は型情報、未登録の場合は nullptr
         */
        const TypeInfo* get(std::type_index type) const noexcept;

        /** @brief 登録済みの型情報をすべて削除する。 */
        void clear() noexcept;

    private:

        std::unordered_map<std::type_index, TypeInfo> m_types;
    };
} // namespace Engine