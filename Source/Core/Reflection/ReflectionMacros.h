#pragma once

#include "Core\Reflection\TypeRegistry.h"

namespace Engine
{
    /**
     * @brief メンバ変数からPropertyを生成する
     * @param name メンバの表示名
     * @return Property 生成したメンバ情報
     */
    template <auto Member>
    Property makeProperty(std::string name)
    {
        using Traits = MemberPointerTraits<decltype(Member)>;
        using ClassType = typename Traits::Class;
        using MemberType = typename Traits::Member;

        const Property::GetFunction getter = [](const void* instance) -> Any
            {
                return Any(static_cast<const ClassType*>(instance)->*Member);
            };

        const Property::SetFunction setter = [](void* instance, const Any& value)
            {
                if constexpr (std::is_copy_assignable_v<MemberType>)
                {
                    if (const MemberType* typed = value.tryGet<MemberType>())
                        static_cast<ClassType*>(instance)->*Member = *typed;
                }
            };

        const Property::AddressFunction address = [](void* instance) -> void*
            {
                return &(static_cast<ClassType*>(instance)->*Member);
            };

        return Property(
            std::move(name),
            typeIdOf<MemberType>(),
            std::string(typeNameOf<MemberType>()),
            sizeof(MemberType),
            getter,
            setter,
            address);
    }

    /**
     * @brief メンバ関数を Any 経由で呼び出すための補助
     */
    template <class Signature>
    struct MethodInvoker;

    template <class ClassType, class ReturnType, class... Args>
    struct MethodInvoker<ReturnType(ClassType::*)(Args...)>
    {
        using Pointer = ReturnType(ClassType::*)(Args...);

        template <Pointer Member>
        static Any invoke(void* instance, [[maybe_unused]] const Any* args, [[maybe_unused]] size_t count)
        {
            return invokeImpl<Member>(instance, args, std::index_sequence_for<Args...>{});
        }

        template <Pointer Member, size_t... Index>
        static Any invokeImpl(void* instance, [[maybe_unused]] const Any* args, std::index_sequence<Index...>)
        {
            ClassType* object = static_cast<ClassType*>(instance);

            if constexpr (std::is_void_v<ReturnType>)
            {
                (object->*Member)(args[Index].template get<std::decay_t<Args>>()...);
                return Any();
            }
            else
            {
                return Any((object->*Member)(args[Index].template get<std::decay_t<Args>>()...));
            }
        }
    };

    template <class ClassType, class ReturnType, class... Args>
    struct MethodInvoker<ReturnType(ClassType::*)(Args...) const>
    {
        using Pointer = ReturnType(ClassType::*)(Args...) const;

        template <Pointer Member>
        static Any invoke(void* instance, [[maybe_unused]] const Any* args, [[maybe_unused]] size_t count)
        {
            return invokeImpl<Member>(instance, args, std::index_sequence_for<Args...>{});
        }

        template <Pointer Member, size_t... Index>
        static Any invokeImpl(void* instance, [[maybe_unused]] const Any* args, std::index_sequence<Index...>)
        {
            const ClassType* object = static_cast<const ClassType*>(instance);

            if constexpr (std::is_void_v<ReturnType>)
            {
                (object->*Member)(args[Index].template get<std::decay_t<Args>>()...);
                return Any();
            }
            else
            {
                return Any((object->*Member)(args[Index].template get<std::decay_t<Args>>()...));
            }
        }
    };

    /**
     * @brief メンバ関数からMethodを生成する
     * @param name メソッドの表示名
     * @return Method 生成したメソッド情報
     */
    template <auto MethodPointer>
    Method makeMethod(std::string name)
    {
        using Pointer = decltype(MethodPointer);
        using Traits = MethodPointerTraits<Pointer>;

        return Method(
            std::move(name),
            Traits::ARG_COUNT,
            typeIdOf<typename Traits::Return>(),
            Traits::IS_CONST,
            &MethodInvoker<Pointer>::template invoke<MethodPointer>);
    }
} // namespace Engine

//! クラス内に静的な型情報の取得手段を追加する（基底クラス用）
#define GE_REFLECT_DECLARE(TypeName)                                                          \
public:                                                                                       \
    static const ::Engine::Type* staticType() { return ::Engine::typeOf<TypeName>(); }        \
    virtual const ::Engine::Type* getType() const { return staticType(); }                    \
private:

//! クラス内に静的な型情報の取得手段を追加する（派生クラス用）
#define GE_REFLECT_OVERRIDE(TypeName)                                                         \
public:                                                                                       \
    static const ::Engine::Type* staticType() { return ::Engine::typeOf<TypeName>(); }        \
    const ::Engine::Type* getType() const override { return staticType(); }                   \
private:

//! 型の登録を開始する（.cppの名前空間スコープに記述する）
#define GE_REFLECT_BEGIN(TypeName)                                                            \
    namespace ReflectionScope_##TypeName                                                      \
    {                                                                                         \
        struct Registrar                                                                      \
        {                                                                                     \
            using ReflectedType = TypeName;                                                   \
            Registrar()                                                                       \
            {                                                                                 \
                ::Engine::Type& type = ::Engine::TypeRegistry::instance()                     \
                    .registerType<ReflectedType>(std::string(::Engine::typeNameOf<ReflectedType>())); \
                (void)type;

//! 基底クラスを登録する
#define GE_REFLECT_BASE(BaseTypeName)                                                         \
                type.setBaseType(&::Engine::TypeRegistry::instance()                          \
                    .registerType<BaseTypeName>(std::string(::Engine::typeNameOf<BaseTypeName>())));

//! メンバ変数を登録する
#define GE_REFLECT_PROPERTY(MemberName)                                                       \
                type.addProperty(::Engine::makeProperty<&ReflectedType::MemberName>(#MemberName));

//! 表示名を指定してメンバ変数を登録する
#define GE_REFLECT_PROPERTY_NAMED(MemberName, DisplayName)                                    \
                type.addProperty(::Engine::makeProperty<&ReflectedType::MemberName>(DisplayName));

//! メンバ関数を登録する
#define GE_REFLECT_METHOD(MethodName)                                                         \
                type.addMethod(::Engine::makeMethod<&ReflectedType::MethodName>(#MethodName));

//! 表示名を指定してメンバ関数を登録する
#define GE_REFLECT_METHOD_NAMED(MethodName, DisplayName)                                      \
                type.addMethod(::Engine::makeMethod<&ReflectedType::MethodName>(DisplayName));

//! 型の登録を終了する
#define GE_REFLECT_END()                                                                      \
            }                                                                                 \
        };                                                                                    \
        static const Registrar g_registrar;                                                   \
    }

//! 列挙型の登録を開始する（.cppの名前空間スコープに記述する）
#define GE_REFLECT_ENUM_BEGIN(EnumName)                                                       \
    namespace ReflectionScope_##EnumName                                                      \
    {                                                                                         \
        struct Registrar                                                                      \
        {                                                                                     \
            using ReflectedType = EnumName;                                                   \
            Registrar()                                                                       \
            {                                                                                 \
                ::Engine::EnumType& type = ::Engine::TypeRegistry::instance()                 \
                    .registerEnum<ReflectedType>(std::string(::Engine::typeNameOf<ReflectedType>())); \
                (void)type;

//! 列挙子を登録する
#define GE_REFLECT_ENUM_VALUE(ValueName)                                                      \
                type.addEntry(#ValueName, static_cast<int64_t>(ReflectedType::ValueName));

//! 列挙型の登録を終了する
#define GE_REFLECT_ENUM_END()                                                                 \
            }                                                                                 \
        };                                                                                    \
        static const Registrar g_registrar;                                                   \
    }
