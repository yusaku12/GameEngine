#pragma once

#include "Core\Memory\MemoryApi.h"

namespace Engine
{
    /**
     * @brief 動的配列
     * エンジンのメモリシステムから確保する std::vector 相当のコンテナ
     */
    template <class T>
    class Array
    {
    public:

        using ValueType = T;
        using Iterator = T*;
        using ConstIterator = const T*;

        //! 見つからなかったことを表す添字
        static constexpr size_t NPOS = ~static_cast<size_t>(0);

        Array() = default;

        /**
         * @brief 確保元を指定して構築する
         * @param allocator 確保元のアロケータ
         * @param tag 用途タグ
         */
        explicit Array(IAllocator& allocator, MemoryTag tag = MemoryTag::CONTAINER)
            : m_allocator(&allocator)
            , m_tag(tag)
        {
        }

        Array(std::initializer_list<T> values)
        {
            reserve(values.size());
            for (const T& value : values)
                add(value);
        }

        Array(const Array& other)
            : m_allocator(other.m_allocator)
            , m_tag(other.m_tag)
        {
            reserve(other.m_size);
            for (size_t index = 0; index < other.m_size; ++index)
                new (m_data + index) T(other.m_data[index]);

            m_size = other.m_size;
        }

        Array(Array&& other) noexcept
            : m_data(other.m_data)
            , m_size(other.m_size)
            , m_capacity(other.m_capacity)
            , m_allocator(other.m_allocator)
            , m_tag(other.m_tag)
        {
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        ~Array()
        {
            destroyAll();
            releaseStorage();
        }

        Array& operator=(const Array& other)
        {
            if (this == &other)
                return *this;

            clear();
            reserve(other.m_size);

            for (size_t index = 0; index < other.m_size; ++index)
                new (m_data + index) T(other.m_data[index]);

            m_size = other.m_size;
            return *this;
        }

        Array& operator=(Array&& other) noexcept
        {
            if (this == &other)
                return *this;

            destroyAll();
            releaseStorage();

            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_allocator = other.m_allocator;
            m_tag = other.m_tag;

            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;

            return *this;
        }

        /**
         * @brief 容量を確保する
         * @param capacity 確保する容量
         */
        void reserve(size_t capacity)
        {
            if (capacity > m_capacity)
                reallocate(capacity);
        }

        /**
         * @brief 要素数を変更する（増えた分は既定値で初期化する）
         * @param size 新しい要素数
         */
        void resize(size_t size)
        {
            if (size < m_size)
            {
                for (size_t index = size; index < m_size; ++index)
                    m_data[index].~T();
            }
            else if (size > m_size)
            {
                reserve(size);
                for (size_t index = m_size; index < size; ++index)
                    new (m_data + index) T();
            }

            m_size = size;
        }

        /**
         * @brief 要素数を変更する（増えた分は指定値で初期化する）
         * @param size 新しい要素数
         * @param value 初期値
         */
        void resize(size_t size, const T& value)
        {
            if (size < m_size)
            {
                for (size_t index = size; index < m_size; ++index)
                    m_data[index].~T();
            }
            else if (size > m_size)
            {
                reserve(size);
                for (size_t index = m_size; index < size; ++index)
                    new (m_data + index) T(value);
            }

            m_size = size;
        }

        /**
         * @brief 容量を要素数まで切り詰める
         */
        void shrinkToFit()
        {
            if (m_size < m_capacity)
                reallocate(m_size);
        }

        /**
         * @brief 全要素を破棄する（容量は維持する）
         */
        void clear()
        {
            destroyAll();
            m_size = 0;
        }

        /**
         * @brief 末尾に要素を追加する
         * @param value 追加する値
         * @return T& 追加した要素
         */
        T& add(const T& value)
        {
            growIfNeeded();
            new (m_data + m_size) T(value);
            return m_data[m_size++];
        }

        /**
         * @brief 末尾に要素を追加する
         * @param value 追加する値
         * @return T& 追加した要素
         */
        T& add(T&& value)
        {
            growIfNeeded();
            new (m_data + m_size) T(std::move(value));
            return m_data[m_size++];
        }

        /**
         * @brief 末尾に要素を直接構築する
         * @param args コンストラクタ引数
         * @return T& 構築した要素
         */
        template <class... Args>
        T& emplace(Args&&... args)
        {
            growIfNeeded();
            new (m_data + m_size) T(std::forward<Args>(args)...);
            return m_data[m_size++];
        }

        /**
         * @brief 別の配列の全要素を末尾へ追加する
         * @param other 追加元の配列
         */
        void append(const Array& other)
        {
            reserve(m_size + other.m_size);
            for (size_t index = 0; index < other.m_size; ++index)
                add(other.m_data[index]);
        }

        /**
         * @brief 指定位置へ要素を挿入する
         * @param index 挿入位置
         * @param value 挿入する値
         */
        void insert(size_t index, const T& value)
        {
            GE_ASSERT(index <= m_size);

            if (index >= m_size)
            {
                add(value);
                return;
            }

            growIfNeeded();

            new (m_data + m_size) T(std::move(m_data[m_size - 1]));
            for (size_t current = m_size - 1; current > index; --current)
                m_data[current] = std::move(m_data[current - 1]);

            m_data[index] = value;
            ++m_size;
        }

        /**
         * @brief 指定位置の要素を取り除く（順序を維持する）
         * @param index 取り除く位置
         */
        void removeAt(size_t index)
        {
            GE_ASSERT(index < m_size);

            if (index >= m_size)
                return;

            for (size_t current = index; current + 1 < m_size; ++current)
                m_data[current] = std::move(m_data[current + 1]);

            m_data[--m_size].~T();
        }

        /**
         * @brief 指定位置の要素を末尾と入れ替えて取り除く（順序は維持しない）
         * @param index 取り除く位置
         */
        void removeAtSwap(size_t index)
        {
            GE_ASSERT(index < m_size);

            if (index >= m_size)
                return;

            if (index != m_size - 1)
                m_data[index] = std::move(m_data[m_size - 1]);

            m_data[--m_size].~T();
        }

        /**
         * @brief 値が一致する最初の要素を取り除く
         * @param value 取り除く値
         * @return bool 取り除いたらtrue
         */
        bool remove(const T& value)
        {
            const size_t index = indexOf(value);
            if (index == NPOS)
                return false;

            removeAt(index);
            return true;
        }

        /**
         * @brief 末尾の要素を取り除く
         */
        void pop()
        {
            if (m_size == 0)
                return;

            m_data[--m_size].~T();
        }

        /**
         * @brief 値が一致する最初の要素の位置を求める
         * @param value 探す値
         * @return size_t 位置。見つからなければNPOS
         */
        size_t indexOf(const T& value) const
        {
            for (size_t index = 0; index < m_size; ++index)
            {
                if (m_data[index] == value)
                    return index;
            }

            return NPOS;
        }

        /**
         * @brief 値が含まれるかを判定する
         * @param value 探す値
         * @return bool 含まれていればtrue
         */
        bool contains(const T& value) const { return indexOf(value) != NPOS; }

        T& operator[](size_t index)
        {
            GE_ASSERT(index < m_size);
            return m_data[index];
        }

        const T& operator[](size_t index) const
        {
            GE_ASSERT(index < m_size);
            return m_data[index];
        }

        T& front() { GE_ASSERT(m_size > 0); return m_data[0]; }

        const T& front() const { GE_ASSERT(m_size > 0); return m_data[0]; }

        T& back() { GE_ASSERT(m_size > 0); return m_data[m_size - 1]; }

        const T& back() const { GE_ASSERT(m_size > 0); return m_data[m_size - 1]; }

        T* data() { return m_data; }

        const T* data() const { return m_data; }

        size_t size() const { return m_size; }

        size_t capacity() const { return m_capacity; }

        bool isEmpty() const { return m_size == 0; }

        Iterator begin() { return m_data; }

        Iterator end() { return m_data + m_size; }

        ConstIterator begin() const { return m_data; }

        ConstIterator end() const { return m_data + m_size; }

    private:

        /**
         * @brief 確保元のアロケータを取得する
         * @return IAllocator& アロケータ
         */
        IAllocator& allocator()
        {
            return m_allocator != nullptr ? *m_allocator : MemoryManager::instance().getPersistentAllocator();
        }

        /**
         * @brief 必要に応じて容量を増やす
         */
        void growIfNeeded()
        {
            if (m_size == m_capacity)
                reallocate(m_capacity == 0 ? 4 : m_capacity * 2);
        }

        /**
         * @brief 容量を変更する
         * @param newCapacity 新しい容量
         */
        void reallocate(size_t newCapacity)
        {
            if (newCapacity == 0)
            {
                releaseStorage();
                m_capacity = 0;
                return;
            }

            memorySetSource(nullptr, 0, m_tag);
            void* memory = memoryAllocateFrom(allocator(), sizeof(T) * newCapacity, memoryAlignmentOf<T>());
            GE_ASSERT_MSG(memory != nullptr, "配列の確保に失敗しました ({} 要素)", newCapacity);

            if (memory == nullptr)
                return;

            T* newData = static_cast<T*>(memory);

            for (size_t index = 0; index < m_size; ++index)
            {
                new (newData + index) T(std::move_if_noexcept(m_data[index]));
                m_data[index].~T();
            }

            releaseStorage();

            m_data = newData;
            m_capacity = newCapacity;
        }

        /**
         * @brief 全要素を破棄する
         */
        void destroyAll()
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (size_t index = 0; index < m_size; ++index)
                    m_data[index].~T();
            }
        }

        /**
         * @brief 確保済みのメモリを解放する
         */
        void releaseStorage()
        {
            if (m_data != nullptr)
            {
                memoryFree(m_data);
                m_data = nullptr;
            }
        }

        T* m_data = nullptr;                //!< 要素の格納先
        size_t      m_size = 0;                      //!< 要素数
        size_t      m_capacity = 0;                  //!< 確保済みの容量
        IAllocator* m_allocator = nullptr;           //!< 確保元のアロケータ
        MemoryTag   m_tag = MemoryTag::CONTAINER;    //!< 用途タグ
    };
} // namespace Engine
