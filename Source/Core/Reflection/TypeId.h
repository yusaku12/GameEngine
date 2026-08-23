#pragma once

#include "Core\Containers\Hash.h"

namespace Engine
{
    //! 型を一意に識別する値
    using TypeId = uint64_t;

    /**
     * @brief 型名を取得する（コンパイル時に決まる）
     * @return std::string_view 名前空間を含む型名
     */
    template <class T>
    constexpr std::string_view typeNameOf()
    {
        constexpr std::string_view signature = __FUNCSIG__;
        constexpr std::string_view prefix = "typeNameOf<";

        const size_t begin = signature.find(prefix) + prefix.size();
        const size_t end = signature.rfind(">(");

        std::string_view name = signature.substr(begin, end - begin);

        // MSVCが付与する「class」「struct」などの修飾を取り除く
        constexpr std::string_view keywords[] = { "class ", "struct ", "enum ", "union " };
        for (std::string_view keyword : keywords)
        {
            if (name.starts_with(keyword))
            {
                name.remove_prefix(keyword.size());
                break;
            }
        }

        return name;
    }

    /**
     * @brief 型を識別する値を取得する（コンパイル時に決まる）
     * @return TypeId 型の識別値
     */
    template <class T>
    constexpr TypeId typeIdOf()
    {
        constexpr std::string_view name = typeNameOf<T>();
        return hashString(name.data(), name.size());
    }

    /**
     * @brief メンバポインタからクラス型とメンバ型を取り出す
     */
    template <class T>
    struct MemberPointerTraits;

    template <class ClassType, class MemberType>
    struct MemberPointerTraits<MemberType ClassType::*>
    {
        using Class = ClassType;
        using Member = MemberType;
    };

    /**
     * @brief メンバ関数ポインタから各種の型情報を取り出す
     */
    template <class T>
    struct MethodPointerTraits;

    template <class ClassType, class ReturnType, class... Args>
    struct MethodPointerTraits<ReturnType(ClassType::*)(Args...)>
    {
        using Class = ClassType;
        using Return = ReturnType;
        static constexpr size_t ARG_COUNT = sizeof...(Args);
        static constexpr bool IS_CONST = false;
    };

    template <class ClassType, class ReturnType, class... Args>
    struct MethodPointerTraits<ReturnType(ClassType::*)(Args...) const>
    {
        using Class = ClassType;
        using Return = ReturnType;
        static constexpr size_t ARG_COUNT = sizeof...(Args);
        static constexpr bool IS_CONST = true;
    };
} // namespace Engine
