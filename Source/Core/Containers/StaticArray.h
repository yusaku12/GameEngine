#pragma once

#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

#include "Core\CoreDefines.h"
#include "Core\Logging\Assert.h"

namespace Engine
{

/**
 * @brief 固定容量の配列
 * 動的確保を一切行わないため、フレーム内の一時領域や小規模な集合に適する
 */
template <class T, size_t CAPACITY>
class StaticArray
{
public:

    using ValueType = T;
    using Iterator = T*;
    using ConstIterator = const T*;

    //! 見つからなかったことを表す添字
    static constexpr size_t NPOS = ~static_cast<size_t>(0);

    StaticArray() = default;

    StaticArray(std::initializer_list<T> values)
    {
        for (const T& value : values)
            add(value);
    }

    StaticArray(const StaticArray& other)
    {
        for (size_t index = 0; index < other.m_size; ++index)
            new (pointerAt(index)) T(other[index]);

        m_size = other.m_size;
    }

    StaticArray(StaticArray&& other) noexcept
    {
        for (size_t index = 0; index < other.m_size; ++index)
            new (pointerAt(index)) T(std::move(other[index]));

        m_size = other.m_size;
        other.clear();
    }

    ~StaticArray()
    {
        clear();
    }

    StaticArray& operator=(const StaticArray& other)
    {
        if (this == &other)
            return *this;

        clear();
        for (size_t index = 0; index < other.m_size; ++index)
            new (pointerAt(index)) T(other[index]);

        m_size = other.m_size;
        return *this;
    }

    StaticArray& operator=(StaticArray&& other) noexcept
    {
        if (this == &other)
            return *this;

        clear();
        for (size_t index = 0; index < other.m_size; ++index)
            new (pointerAt(index)) T(std::move(other[index]));

        m_size = other.m_size;
        other.clear();
        return *this;
    }

    /**
     * @brief 末尾に要素を追加する
     * @param value 追加する値
     * @return T* 追加した要素。容量不足の場合はnullptr
     */
    T* add(const T& value)
    {
        GE_ASSERT_MSG(m_size < CAPACITY, "StaticArrayの容量({})を超えました", CAPACITY);

        if (m_size >= CAPACITY)
            return nullptr;

        return new (pointerAt(m_size++)) T(value);
    }

    /**
     * @brief 末尾に要素を直接構築する
     * @param args コンストラクタ引数
     * @return T* 構築した要素。容量不足の場合はnullptr
     */
    template <class... Args>
    T* emplace(Args&&... args)
    {
        GE_ASSERT_MSG(m_size < CAPACITY, "StaticArrayの容量({})を超えました", CAPACITY);

        if (m_size >= CAPACITY)
            return nullptr;

        return new (pointerAt(m_size++)) T(std::forward<Args>(args)...);
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
            (*this)[index] = std::move((*this)[m_size - 1]);

        (*this)[--m_size].~T();
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
            (*this)[current] = std::move((*this)[current + 1]);

        (*this)[--m_size].~T();
    }

    /**
     * @brief 末尾の要素を取り除く
     */
    void pop()
    {
        if (m_size == 0)
            return;

        (*this)[--m_size].~T();
    }

    /**
     * @brief 全要素を破棄する
     */
    void clear()
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_t index = 0; index < m_size; ++index)
                (*this)[index].~T();
        }

        m_size = 0;
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
            if ((*this)[index] == value)
                return index;
        }

        return NPOS;
    }

    bool contains(const T& value) const { return indexOf(value) != NPOS; }

    T& operator[](size_t index)
    {
        GE_ASSERT(index < m_size);
        return *pointerAt(index);
    }

    const T& operator[](size_t index) const
    {
        GE_ASSERT(index < m_size);
        return *pointerAt(index);
    }

    T& front() { GE_ASSERT(m_size > 0); return (*this)[0]; }
    const T& front() const { GE_ASSERT(m_size > 0); return (*this)[0]; }
    T& back() { GE_ASSERT(m_size > 0); return (*this)[m_size - 1]; }
    const T& back() const { GE_ASSERT(m_size > 0); return (*this)[m_size - 1]; }

    T* data() { return pointerAt(0); }
    const T* data() const { return pointerAt(0); }

    size_t size() const { return m_size; }
    static constexpr size_t capacity() { return CAPACITY; }
    bool isEmpty() const { return m_size == 0; }
    bool isFull() const { return m_size == CAPACITY; }

    Iterator begin() { return data(); }
    Iterator end() { return data() + m_size; }
    ConstIterator begin() const { return data(); }
    ConstIterator end() const { return data() + m_size; }

private:

    T* pointerAt(size_t index) { return reinterpret_cast<T*>(m_storage) + index; }
    const T* pointerAt(size_t index) const { return reinterpret_cast<const T*>(m_storage) + index; }

    alignas(T) unsigned char m_storage[sizeof(T) * CAPACITY]{}; //!< 要素の格納先
    size_t                   m_size = 0;                        //!< 要素数
};

} // namespace Engine
