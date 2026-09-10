#pragma once

#include <cstddef>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Engine
{
    /** @brief リフレクションで扱うプロパティの型。 */
    enum class PropertyType
    {
        Boolean,
        Integer,
        UnsignedInteger,
        Float,
        Vector3,
        Quaternion,
        String
    };

    /** @brief リフレクション対象プロパティのメタデータ。 */
    struct PropertyInfo
    {
        std::string_view name;
        PropertyType type = PropertyType::Float;
        std::size_t offset = 0;
        std::size_t size = 0;
        bool editable = false;
    };

    /** @brief 型名と所属プロパティのメタデータ。 */
    struct TypeInfo
    {
        std::string_view name;
        std::vector<PropertyInfo> properties;
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
         * @param name エディタで表示する型名
         * @return 登録された型情報
         */
        template <typename T>
        TypeInfo& registerType(std::string_view name)
        {
            return m_types[std::type_index(typeid(T))] = TypeInfo{ name, {} };
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

        /** @brief std::type_indexから型情報を取得する。 */
        [[nodiscard]] const TypeInfo* get(std::type_index type) const noexcept;
        /** @brief 登録済みの型情報をすべて削除する。 */
        void clear() noexcept;

    private:
        std::unordered_map<std::type_index, TypeInfo> m_types;
    };
} // namespace Engine