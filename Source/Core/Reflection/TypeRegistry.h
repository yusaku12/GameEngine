#pragma once

#include "Core\Reflection\Type.h"

namespace Engine
{
    /**
     * @brief 型情報の登録先
     * 静的初期化の時点から利用するため、内部では標準ライブラリのコンテナを使用している
     * （エンジンのヒープはMemoryManagerの初期化後にしか使えないため）
     */
    class TypeRegistry
    {
    public:

        /**
         * @brief インスタンスを取得する
         * @return TypeRegistry& インスタンス
         */
        static TypeRegistry& instance()
        {
            static TypeRegistry instance;
            return instance;
        }

        /**
         * @brief 型を登録する（既に登録済みならそれを返す）
         * @param name 型名
         * @return Type& 登録した型情報
         */
        template <class T>
        Type& registerType(std::string name)
        {
            constexpr TypeId id = typeIdOf<T>();

            if (Type* existing = findMutableType(id))
                return *existing;

            auto type = std::make_unique<Type>(std::move(name), id, sizeof(T), alignof(T));

            if constexpr (std::is_default_constructible_v<T>)
            {
                type->setLifetime(
                    []() -> void* { return memoryNew<T>(); },
                    [](void* instance) { memoryDelete(static_cast<T*>(instance)); });
            }

            Type* result = type.get();
            m_typesByName.emplace(result->getName(), result);
            m_types.emplace(id, std::move(type));

            return *result;
        }

        /**
         * @brief 列挙型を登録する（既に登録済みならそれを返す）
         * @param name 型名
         * @return EnumType& 登録した列挙型情報
         */
        template <class T>
        EnumType& registerEnum(std::string name)
        {
            constexpr TypeId id = typeIdOf<T>();

            auto found = m_enums.find(id);
            if (found != m_enums.end())
                return *found->second;

            auto type = std::make_unique<EnumType>(std::move(name), id);
            EnumType* result = type.get();

            m_enumsByName.emplace(result->getName(), result);
            m_enums.emplace(id, std::move(type));

            return *result;
        }

        /**
         * @brief 識別値から型情報を探す
         * @param id 型の識別値
         * @return const Type* 見つかった型情報。無ければnullptr
         */
        const Type* findType(TypeId id) const;

        /**
         * @brief 名前から型情報を探す
         * @param name 型名
         * @return const Type* 見つかった型情報。無ければnullptr
         */
        const Type* findType(std::string_view name) const;

        /**
         * @brief 識別値から列挙型情報を探す
         * @param id 型の識別値
         * @return const EnumType* 見つかった列挙型情報。無ければnullptr
         */
        const EnumType* findEnum(TypeId id) const;

        /**
         * @brief 名前から列挙型情報を探す
         * @param name 型名
         * @return const EnumType* 見つかった列挙型情報。無ければnullptr
         */
        const EnumType* findEnum(std::string_view name) const;

        /**
         * @brief 登録されている型の数を取得する
         * @return size_t 型の数
         */
        size_t getTypeCount() const { return m_types.size(); }

        /**
         * @brief 登録されている型の一覧をログへ出力する
         */
        void dump() const;

    private:

        TypeRegistry() = default;
        ~TypeRegistry() = default;

        GE_DISABLE_COPY_AND_MOVE(TypeRegistry);

        Type* findMutableType(TypeId id);

        std::unordered_map<TypeId, std::unique_ptr<Type>>     m_types;       //!< 識別値から型情報への対応
        std::unordered_map<std::string, Type*>                m_typesByName; //!< 名前から型情報への対応
        std::unordered_map<TypeId, std::unique_ptr<EnumType>> m_enums;       //!< 識別値から列挙型情報への対応
        std::unordered_map<std::string, EnumType*>            m_enumsByName; //!< 名前から列挙型情報への対応
    };

    /**
     * @brief 型情報を取得する
     * @return const Type* 型情報。登録されていなければnullptr
     */
    template <class T>
    const Type* typeOf()
    {
        return TypeRegistry::instance().findType(typeIdOf<T>());
    }

    /**
     * @brief 列挙型情報を取得する
     * @return const EnumType* 列挙型情報。登録されていなければnullptr
     */
    template <class T>
    const EnumType* enumOf()
    {
        return TypeRegistry::instance().findEnum(typeIdOf<T>());
    }
} // namespace Engine
