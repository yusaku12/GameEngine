#pragma once

#include "Core\Memory\MemoryApi.h"

namespace Engine
{
    /**
     * @brief 固定容量のリングバッファ
     * 先頭と末尾の双方から出し入れでき、容量を超える追加は失敗する
     */
    template <class T>
    class RingBuffer
    {
    public:

        RingBuffer() = default;

        /**
         * @brief 容量を指定して構築する
         * @param capacity 格納できる要素数
         * @param allocator 確保元のアロケータ
         * @param tag 用途タグ
         */
        explicit RingBuffer(size_t capacity, IAllocator* allocator = nullptr, MemoryTag tag = MemoryTag::CONTAINER)
            : m_allocator(allocator)
            , m_tag(tag)
        {
            initialize(capacity);
        }

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        RingBuffer(RingBuffer&& other) noexcept
            : m_data(other.m_data)
            , m_capacity(other.m_capacity)
            , m_head(other.m_head)
            , m_size(other.m_size)
            , m_allocator(other.m_allocator)
            , m_tag(other.m_tag)
        {
            other.m_data = nullptr;
            other.m_capacity = 0;
            other.m_head = 0;
            other.m_size = 0;
        }

        RingBuffer& operator=(RingBuffer&& other) noexcept
        {
            if (this == &other)
                return *this;

            finalize();

            m_data = other.m_data;
            m_capacity = other.m_capacity;
            m_head = other.m_head;
            m_size = other.m_size;
            m_allocator = other.m_allocator;
            m_tag = other.m_tag;

            other.m_data = nullptr;
            other.m_capacity = 0;
            other.m_head = 0;
            other.m_size = 0;

            return *this;
        }

        ~RingBuffer()
        {
            finalize();
        }

        /**
         * @brief 容量を確保する（既存の内容は破棄される）
         * @param capacity 格納できる要素数
         * @return bool 成功したらtrue
         */
        bool initialize(size_t capacity)
        {
            finalize();

            if (capacity == 0)
                return false;

            memorySetSource(nullptr, 0, m_tag);
            IAllocator& target = m_allocator != nullptr ? *m_allocator : MemoryManager::instance().getPersistentAllocator();
            void* memory = memoryAllocateFrom(target, sizeof(T) * capacity, memoryAlignmentOf<T>());

            if (memory == nullptr)
                return false;

            m_data = static_cast<T*>(memory);
            m_capacity = capacity;
            return true;
        }

        /**
         * @brief 内容と確保したメモリを破棄する
         */
        void finalize()
        {
            clear();

            if (m_data != nullptr)
            {
                memoryFree(m_data);
                m_data = nullptr;
            }

            m_capacity = 0;
        }

        /**
         * @brief 末尾へ要素を追加する
         * @param value 追加する値
         * @return bool 追加できたらtrue
         */
        bool pushBack(const T& value)
        {
            if (isFull())
                return false;

            new (m_data + indexOf(m_size)) T(value);
            ++m_size;
            return true;
        }

        /**
         * @brief 末尾へ要素を直接構築する
         * @param args コンストラクタ引数
         * @return bool 追加できたらtrue
         */
        template <class... Args>
        bool emplaceBack(Args&&... args)
        {
            if (isFull())
                return false;

            new (m_data + indexOf(m_size)) T(std::forward<Args>(args)...);
            ++m_size;
            return true;
        }

        /**
         * @brief 先頭へ要素を追加する
         * @param value 追加する値
         * @return bool 追加できたらtrue
         */
        bool pushFront(const T& value)
        {
            if (isFull())
                return false;

            m_head = m_head == 0 ? m_capacity - 1 : m_head - 1;
            new (m_data + m_head) T(value);
            ++m_size;
            return true;
        }

        /**
         * @brief 先頭の要素を取り出す
         * @param outValue 取り出した値の格納先
         * @return bool 取り出せたらtrue
         */
        bool popFront(T& outValue)
        {
            if (isEmpty())
                return false;

            outValue = std::move(m_data[m_head]);
            m_data[m_head].~T();

            m_head = (m_head + 1) % m_capacity;
            --m_size;
            return true;
        }

        /**
         * @brief 末尾の要素を取り出す
         * @param outValue 取り出した値の格納先
         * @return bool 取り出せたらtrue
         */
        bool popBack(T& outValue)
        {
            if (isEmpty())
                return false;

            const size_t index = indexOf(m_size - 1);
            outValue = std::move(m_data[index]);
            m_data[index].~T();

            --m_size;
            return true;
        }

        /**
         * @brief 全要素を破棄する（容量は維持する）
         */
        void clear()
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (size_t offset = 0; offset < m_size; ++offset)
                    m_data[indexOf(offset)].~T();
            }

            m_head = 0;
            m_size = 0;
        }

        T& operator[](size_t index)
        {
            GE_ASSERT(index < m_size);
            return m_data[indexOf(index)];
        }

        const T& operator[](size_t index) const
        {
            GE_ASSERT(index < m_size);
            return m_data[indexOf(index)];
        }

        T& front() { GE_ASSERT(m_size > 0); return m_data[m_head]; }
        const T& front() const { GE_ASSERT(m_size > 0); return m_data[m_head]; }
        T& back() { GE_ASSERT(m_size > 0); return m_data[indexOf(m_size - 1)]; }
        const T& back() const { GE_ASSERT(m_size > 0); return m_data[indexOf(m_size - 1)]; }

        size_t size() const { return m_size; }
        size_t capacity() const { return m_capacity; }
        bool isEmpty() const { return m_size == 0; }
        bool isFull() const { return m_size == m_capacity; }

    private:

        size_t indexOf(size_t offset) const { return (m_head + offset) % m_capacity; }

        T* m_data = nullptr;              //!< 要素の格納先
        size_t      m_capacity = 0;                //!< 格納できる要素数
        size_t      m_head = 0;                    //!< 先頭の位置
        size_t      m_size = 0;                    //!< 要素数
        IAllocator* m_allocator = nullptr;         //!< 確保元のアロケータ
        MemoryTag   m_tag = MemoryTag::CONTAINER;  //!< 用途タグ
    };
} // namespace Engine
