#pragma once

#include "Core\Reflection\Any.h"
#include "Core\Reflection\TypeId.h"

namespace Engine
{
    class Type;

    /**
     * @brief 反映されたメンバ変数
     */
    class Property
    {
    public:

        using GetFunction = Any(*)(const void*);
        using SetFunction = void (*)(void*, const Any&);
        using AddressFunction = void* (*)(void*);

        Property(std::string name, TypeId typeId, std::string typeName, size_t size, GetFunction getter, SetFunction setter, AddressFunction address)
            : m_name(std::move(name))
            , m_typeName(std::move(typeName))
            , m_typeId(typeId)
            , m_size(size)
            , m_getter(getter)
            , m_setter(setter)
            , m_address(address)
        {
        }

        const std::string& getName() const { return m_name; }
        const std::string& getTypeName() const { return m_typeName; }
        TypeId getTypeId() const { return m_typeId; }
        size_t getSize() const { return m_size; }

        /**
         * @brief 値を取得する
         * @param instance 対象のインスタンス
         * @return Any 取得した値
         */
        Any getValue(const void* instance) const { return m_getter != nullptr ? m_getter(instance) : Any(); }

        /**
         * @brief 値を設定する
         * @param instance 対象のインスタンス
         * @param value 設定する値
         */
        void setValue(void* instance, const Any& value) const
        {
            if (m_setter != nullptr)
                m_setter(instance, value);
        }

        /**
         * @brief メンバへのポインタを取得する
         * @param instance 対象のインスタンス
         * @return T* メンバへのポインタ。型が異なる場合はnullptr
         */
        template <class T>
        T* getPointer(void* instance) const
        {
            if (m_address == nullptr || m_typeId != typeIdOf<T>())
                return nullptr;

            return static_cast<T*>(m_address(instance));
        }

    private:

        std::string     m_name;               //!< メンバ名
        std::string     m_typeName;           //!< メンバの型名
        TypeId          m_typeId;             //!< メンバの型
        size_t          m_size;               //!< メンバのサイズ
        GetFunction     m_getter;             //!< 値を取得する関数
        SetFunction     m_setter;             //!< 値を設定する関数
        AddressFunction m_address;            //!< メンバのアドレスを取得する関数
    };

    /**
     * @brief 反映されたメンバ関数
     */
    class Method
    {
    public:

        using InvokeFunction = Any(*)(void*, const Any*, size_t);

        Method(std::string name, size_t argCount, TypeId returnTypeId, bool isConst, InvokeFunction invoker)
            : m_name(std::move(name))
            , m_argCount(argCount)
            , m_returnTypeId(returnTypeId)
            , m_isConst(isConst)
            , m_invoker(invoker)
        {
        }

        const std::string& getName() const { return m_name; }
        size_t getArgCount() const { return m_argCount; }
        TypeId getReturnTypeId() const { return m_returnTypeId; }
        bool isConst() const { return m_isConst; }

        /**
         * @brief メソッドを呼び出す
         * @param instance 対象のインスタンス
         * @param args 引数の配列
         * @param count 引数の数
         * @return Any 戻り値（voidの場合は値を保持しない）
         */
        Any invoke(void* instance, const Any* args = nullptr, size_t count = 0) const;

        /**
         * @brief 引数を直接指定してメソッドを呼び出す
         * @param instance 対象のインスタンス
         * @param args 引数
         * @return Any 戻り値
         */
        template <class... Args>
        Any call(void* instance, Args&&... args) const
        {
            const Any values[] = { Any(std::forward<Args>(args))..., Any() };
            return invoke(instance, values, sizeof...(Args));
        }

    private:

        std::string    m_name;          //!< メソッド名
        size_t         m_argCount;      //!< 引数の数
        TypeId         m_returnTypeId;  //!< 戻り値の型
        bool           m_isConst;       //!< constメソッドか
        InvokeFunction m_invoker;       //!< 呼び出しを行う関数
    };

    /**
     * @brief 型情報
     * メンバ変数・メンバ関数・基底クラスを保持する
     */
    class Type
    {
    public:

        using ConstructFunction = void* (*)();
        using DestroyFunction = void (*)(void*);

        Type(std::string name, TypeId id, size_t size, size_t alignment)
            : m_name(std::move(name))
            , m_id(id)
            , m_size(size)
            , m_alignment(alignment)
        {
        }

        const std::string& getName() const { return m_name; }
        TypeId getId() const { return m_id; }
        size_t getSize() const { return m_size; }
        size_t getAlignment() const { return m_alignment; }
        const Type* getBaseType() const { return m_baseType; }

        const std::vector<Property>& getProperties() const { return m_properties; }
        const std::vector<Method>& getMethods() const { return m_methods; }

        /**
         * @brief 名前からメンバ変数を探す（基底クラスも辿る）
         * @param name メンバ名
         * @return const Property* 見つかったメンバ。無ければnullptr
         */
        const Property* findProperty(std::string_view name) const;

        /**
         * @brief 名前からメンバ関数を探す（基底クラスも辿る）
         * @param name メソッド名
         * @return const Method* 見つかったメソッド。無ければnullptr
         */
        const Method* findMethod(std::string_view name) const;

        /**
         * @brief 指定した型から派生しているかを判定する
         * @param other 判定する型
         * @return bool 派生していればtrue
         */
        bool isDerivedFrom(const Type& other) const;

        /**
         * @brief 既定コンストラクタでインスタンスを生成する
         * @return void* 生成したインスタンス。生成できない場合はnullptr
         */
        void* construct() const { return m_construct != nullptr ? m_construct() : nullptr; }

        /**
         * @brief インスタンスを破棄する
         * @param instance 破棄するインスタンス
         */
        void destroy(void* instance) const
        {
            if (m_destroy != nullptr && instance != nullptr)
                m_destroy(instance);
        }

        /**
         * @brief インスタンスの内容を文字列化する（基底クラスのメンバも含む）
         * @param instance 対象のインスタンス
         * @return std::string 文字列化した内容
         */
        std::string toString(const void* instance) const;

        // 以下は登録用の操作

        Type& setBaseType(const Type* baseType)
        {
            m_baseType = baseType;
            return *this;
        }

        Type& setLifetime(ConstructFunction construct, DestroyFunction destroy)
        {
            m_construct = construct;
            m_destroy = destroy;
            return *this;
        }

        Type& addProperty(Property property)
        {
            m_properties.push_back(std::move(property));
            return *this;
        }

        Type& addMethod(Method method)
        {
            m_methods.push_back(std::move(method));
            return *this;
        }

    private:

        std::string           m_name;                  //!< 型名
        TypeId                m_id;                    //!< 型の識別値
        size_t                m_size;                  //!< 型のサイズ
        size_t                m_alignment;             //!< 型のアライメント
        const Type* m_baseType = nullptr;    //!< 基底クラスの型情報
        std::vector<Property> m_properties;            //!< メンバ変数
        std::vector<Method>   m_methods;               //!< メンバ関数
        ConstructFunction     m_construct = nullptr;   //!< インスタンスを生成する関数
        DestroyFunction       m_destroy = nullptr;     //!< インスタンスを破棄する関数
    };

    /**
     * @brief 反映された列挙型
     */
    class EnumType
    {
    public:

        /**
         * @brief 列挙子の名前と値の組
         */
        struct Entry
        {
            std::string name;  //!< 列挙子の名前
            int64_t     value; //!< 列挙子の値
        };

        EnumType(std::string name, TypeId id)
            : m_name(std::move(name))
            , m_id(id)
        {
        }

        const std::string& getName() const { return m_name; }
        TypeId getId() const { return m_id; }
        const std::vector<Entry>& getEntries() const { return m_entries; }

        /**
         * @brief 値から列挙子の名前を求める
         * @param value 列挙子の値
         * @return std::string_view 名前。見つからなければ空
         */
        std::string_view toString(int64_t value) const;

        /**
         * @brief 名前から列挙子の値を求める
         * @param name 列挙子の名前
         * @param outValue 値の格納先
         * @return bool 見つかったらtrue
         */
        bool fromString(std::string_view name, int64_t& outValue) const;

        EnumType& addEntry(std::string name, int64_t value)
        {
            m_entries.push_back(Entry{ std::move(name), value });
            return *this;
        }

    private:

        std::string        m_name;    //!< 列挙型の名前
        TypeId             m_id;      //!< 型の識別値
        std::vector<Entry> m_entries; //!< 列挙子の一覧
    };
} // namespace Engine
