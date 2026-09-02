#pragma once

#include "Core\Memory\MemoryApi.h"

namespace Engine
{
    /**
     * @brief エンジンのメモリシステムを利用するSTL用アロケータ
     * 既定では永続ヒープから確保する
     */
    template <class T>
    class StlAllocator
    {
    public:

        using value_type = T;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::false_type;

        /**
         * @brief コンストラクタ
         * 確保元は永続ヒープになる
         */
        StlAllocator() noexcept = default;

        /**
         * @brief コンストラクタ
         * @param allocator 確保元のアロケータ
         * @param tag 用途タグ
         */
        explicit StlAllocator(IAllocator& allocator, MemoryTag tag = MemoryTag::CONTAINER) noexcept
            : m_allocator(&allocator)
            , m_tag(tag)
        {
        }

        /**
         * @brief 別要素型のアロケータからの変換コンストラクタ
         */
        template <class U>
        StlAllocator(const StlAllocator<U>& other) noexcept
            : m_allocator(other.getAllocator())
            , m_tag(other.getTag())
        {
        }

        /**
         * @brief メモリを確保する
         * @param count 要素数
         * @return T* 確保したメモリ
         */
        T* allocate(size_t count)
        {
            IAllocator& allocator = m_allocator != nullptr ? *m_allocator : MemoryManager::instance().getPersistentAllocator();

            memorySetSource(nullptr, 0, m_tag);
            void* pointer = memoryAllocateFrom(allocator, sizeof(T) * count, memoryAlignmentOf<T>());

            if (pointer == nullptr)
                throw std::bad_alloc();

            return static_cast<T*>(pointer);
        }

        /**
         * @brief メモリを解放する
         * @param pointer 解放するメモリ
         */
        void deallocate(T* pointer, size_t) noexcept
        {
            memoryFree(pointer);
        }

        /**
         * @brief 確保元のアロケータを取得する
         * @return IAllocator* 確保元のアロケータ
         */
        IAllocator* getAllocator() const noexcept { return m_allocator; }

        /**
         * @brief 用途タグを取得する
         * @return MemoryTag 用途タグ
         */
        MemoryTag getTag() const noexcept { return m_tag; }

    private:

        IAllocator* m_allocator = nullptr;             //!< 確保元のアロケータ
        MemoryTag   m_tag = MemoryTag::CONTAINER;      //!< 用途タグ
    };

    /**
     * @brief 別要素型のアロケータ同士の比較
     * @return bool 同じ確保元のアロケータを持つ場合はtrue
     */
    template <class T, class U>
    inline bool operator==(const StlAllocator<T>& left, const StlAllocator<U>& right) noexcept
    {
        return left.getAllocator() == right.getAllocator();
    }

    /**
     * @brief 別要素型のアロケータ同士の比較
     * @return bool 同じ確保元のアロケータを持たない場合はtrue
     */
    template <class T, class U>
    inline bool operator!=(const StlAllocator<T>& left, const StlAllocator<U>& right) noexcept
    {
        return !(left == right);
    }
} // namespace Engine
