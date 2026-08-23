#pragma once

#include "Core\Containers\HashMap.h"

namespace Engine
{

/**
 * @brief 値を持たないことを表す型
 */
struct EmptyValue
{
};

/**
 * @brief ハッシュ集合
 * 内部はHashMapを利用しており、重複しない値の集合を管理する
 */
template <class K, class HashFunction = Hash<K>>
class HashSet
{
public:

    using MapType = HashMap<K, EmptyValue, HashFunction>;

    /**
     * @brief 走査用のイテレータ
     */
    class Iterator
    {
    public:

        explicit Iterator(typename MapType::ConstIterator iterator)
            : m_iterator(iterator)
        {
        }

        const K& operator*() const { return m_iterator->key; }
        const K* operator->() const { return &m_iterator->key; }

        Iterator& operator++()
        {
            ++m_iterator;
            return *this;
        }

        bool operator==(const Iterator& other) const { return m_iterator == other.m_iterator; }
        bool operator!=(const Iterator& other) const { return m_iterator != other.m_iterator; }

    private:

        typename MapType::ConstIterator m_iterator; //!< 内部マップのイテレータ
    };

    HashSet() = default;

    /**
     * @brief 確保元を指定して構築する
     * @param allocator 確保元のアロケータ
     * @param tag 用途タグ
     */
    explicit HashSet(IAllocator& allocator, MemoryTag tag = MemoryTag::CONTAINER)
        : m_map(allocator, tag)
    {
    }

    /**
     * @brief 値を追加する
     * @param key 追加する値
     * @return bool 新規に追加したらtrue（既に存在していた場合はfalse）
     */
    bool add(const K& key)
    {
        if (m_map.contains(key))
            return false;

        m_map.insert(key, EmptyValue{});
        return true;
    }

    /**
     * @brief 値が含まれるかを判定する
     * @param key 探す値
     * @return bool 含まれていればtrue
     */
    bool contains(const K& key) const { return m_map.contains(key); }

    /**
     * @brief 値を取り除く
     * @param key 取り除く値
     * @return bool 取り除いたらtrue
     */
    bool remove(const K& key) { return m_map.remove(key); }

    /**
     * @brief 指定した要素数を格納できるよう容量を確保する
     * @param count 要素数
     */
    void reserve(size_t count) { m_map.reserve(count); }

    /**
     * @brief 全要素を破棄する
     */
    void clear() { m_map.clear(); }

    size_t size() const { return m_map.size(); }
    bool isEmpty() const { return m_map.isEmpty(); }

    Iterator begin() const { return Iterator(m_map.begin()); }
    Iterator end() const { return Iterator(m_map.end()); }

private:

    MapType m_map; //!< 内部のハッシュマップ
};

} // namespace Engine
