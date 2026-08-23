#pragma once

#include <any>

#include "Core\Containers\String.h"
#include "Core\Logging\Assert.h"
#include "Core\Reflection\TypeId.h"

namespace Engine
{
    /**
     * @brief 任意の値を文字列へ変換する
     * 数値・真偽値・文字列・列挙型のみ内容を出力し、それ以外は型名を出力する
     */
    template <class T>
    std::string anyValueToString(const std::any& value)
    {
        const T* typed = std::any_cast<T>(&value);
        if (typed == nullptr)
            return "<invalid>";

        if constexpr (std::is_same_v<T, bool>)
            return *typed ? "true" : "false";
        else if constexpr (std::is_enum_v<T>)
            return spdlog::fmt_lib::format("{}", static_cast<std::underlying_type_t<T>>(*typed));
        else if constexpr (std::is_arithmetic_v<T>)
            return spdlog::fmt_lib::format("{}", *typed);
        else if constexpr (std::is_same_v<T, String>)
            return std::string(typed->c_str(), typed->size());
        else if constexpr (std::is_convertible_v<T, std::string_view>)
            return std::string(static_cast<std::string_view>(*typed));
        else
            return spdlog::fmt_lib::format("<{}>", typeNameOf<T>());
    }

    /**
     * @brief 任意の型の値を保持するクラス
     * リフレクション経由でプロパティやメソッドの値をやり取りするために使用する
     */
    class Any
    {
    public:

        Any() = default;

        /**
         * @brief 値を保持して構築する
         * @param value 保持する値
         */
        template <class T, class = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Any>>>
        Any(T&& value)
            : m_value(std::forward<T>(value))
            , m_typeId(typeIdOf<std::decay_t<T>>())
            , m_toString(&anyValueToString<std::decay_t<T>>)
        {
        }

        /**
         * @brief 値を保持しているかを取得する
         * @return bool 保持していればtrue
         */
        bool hasValue() const { return m_value.has_value(); }

        /**
         * @brief 保持している値の型を取得する
         * @return TypeId 型の識別値
         */
        TypeId getTypeId() const { return m_typeId; }

        /**
         * @brief 保持している値が指定した型かを判定する
         * @return bool 指定した型ならtrue
         */
        template <class T>
        bool is() const { return m_typeId == typeIdOf<std::decay_t<T>>(); }

        /**
         * @brief 保持している値へのポインタを取得する
         * @return T* 値へのポインタ。型が異なる場合はnullptr
         */
        template <class T>
        T* tryGet() { return is<T>() ? std::any_cast<T>(&m_value) : nullptr; }

        /**
         * @brief 保持している値へのポインタを取得する
         * @return const T* 値へのポインタ。型が異なる場合はnullptr
         */
        template <class T>
        const T* tryGet() const { return is<T>() ? std::any_cast<T>(&m_value) : nullptr; }

        /**
         * @brief 保持している値を取得する
         * @return T 値。型が異なる場合は既定値
         */
        template <class T>
        T get() const
        {
            const T* typed = tryGet<T>();
            GE_ASSERT_MSG(typed != nullptr, "Anyの型が一致しません（要求: {}）", typeNameOf<T>());

            return typed != nullptr ? *typed : T{};
        }

        /**
         * @brief 保持している値を取得する（型が異なる場合は既定値を返す）
         * @param fallback 型が異なる場合に返す値
         * @return T 値
         */
        template <class T>
        T getOr(T fallback) const
        {
            const T* typed = tryGet<T>();
            return typed != nullptr ? *typed : fallback;
        }

        /**
         * @brief 保持している値を文字列へ変換する
         * @return std::string 変換結果
         */
        std::string toString() const
        {
            return m_toString != nullptr ? m_toString(m_value) : "<empty>";
        }

        /**
         * @brief 保持している値を破棄する
         */
        void reset()
        {
            m_value.reset();
            m_typeId = 0;
            m_toString = nullptr;
        }

    private:

        using ToStringFunction = std::string(*)(const std::any&);

        std::any         m_value;              //!< 保持している値
        TypeId           m_typeId = 0;         //!< 保持している値の型
        ToStringFunction m_toString = nullptr; //!< 文字列化する関数
    };
} // namespace Engine
