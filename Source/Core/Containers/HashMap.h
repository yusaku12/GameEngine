#pragma once

#include <new>
#include <type_traits>
#include <utility>

#include "Core\Containers\Hash.h"
#include "Core\Memory\MemoryApi.h"

namespace Engine
{

/**
 * @brief キーと値の組
 */
template <class K, class V>
struct KeyValue
{
    K key;   //!< キー
    V value; //!< 値
};

/**
 * @brief ハッシュマップ
 * 開番地法とロビンフッドハッシュを用いており、削除時は後方シフトで墓標を残さない
 */
template <class K, class V, class HashFunction = Hash<K>>
class HashMap
{
public:

    using Entry = KeyValue<K, V>;

    //! 見つからなかったことを表す添字
    static constexpr size_t NPOS = ~static_cast<size_t>(0);

    //! 既定の初期容量
    static constexpr size_t DEFAULT_CAPACITY = 16;

    /**
     * @brief 走査用のイテレータ
     */
    template <class MapType, class EntryType>
    class IteratorBase
    {
    public:

        IteratorBase(MapType* map, size_t index)
            : m_map(map)
            , m_index(index)
        {
            skipEmpty();
        }

        EntryType& operator*() const { return m_map->m_slots[m_index]; }
        EntryType* operator->() const { return m_map->m_slots + m_index; }

        IteratorBase& operator++()
        {
            ++m_index;
            skipEmpty();
            return *this;
        }

        bool operator==(const IteratorBase& other) const { return m_index == other.m_index; }
        bool operator!=(const IteratorBase& other) const { return m_index != other.m_index; }

    private:

        void skipEmpty()
        {
            while (m_index < m_map->m_capacity && m_map->m_hashes[m_index] == 0)
                ++m_index;
        }

        MapType* m_map;   //!< 走査対象
        size_t   m_index; //!< 現在位置
    };

    using Iterator = IteratorBase<HashMap, Entry>;
    using ConstIterator = IteratorBase<const HashMap, const Entry>;

    HashMap() = default;

    /**
     * @brief 確保元を指定して構築する
     * @param allocator 確保元のアロケータ
     * @param tag 用途タグ
     */
    explicit HashMap(IAllocator& allocator, MemoryTag tag = MemoryTag::CONTAINER)
        : m_allocator(&allocator)
        , m_tag(tag)
    {
    }

    HashMap(const HashMap& other)
        : m_allocator(other.m_allocator)
        , m_tag(other.m_tag)
    {
        reserve(other.m_size);
        for (ConstIterator it = other.begin(); it != other.end(); ++it)
            insert(it->key, it->value);
    }

    HashMap(HashMap&& other) noexcept
        : m_hashes(other.m_hashes)
        , m_slots(other.m_slots)
        , m_capacity(other.m_capacity)
        , m_size(other.m_size)
        , m_allocator(other.m_allocator)
        , m_tag(other.m_tag)
    {
        other.m_hashes = nullptr;
        other.m_slots = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;
    }

    ~HashMap()
    {
        destroyAll();
        releaseStorage();
    }

    HashMap& operator=(const HashMap& other)
    {
        if (this == &other)
            return *this;

        clear();
        reserve(other.m_size);

        for (ConstIterator it = other.begin(); it != other.end(); ++it)
            insert(it->key, it->value);

        return *this;
    }

    HashMap& operator=(HashMap&& other) noexcept
    {
        if (this == &other)
            return *this;

        destroyAll();
        releaseStorage();

        m_hashes = other.m_hashes;
        m_slots = other.m_slots;
        m_capacity = other.m_capacity;
        m_size = other.m_size;
        m_allocator = other.m_allocator;
        m_tag = other.m_tag;

        other.m_hashes = nullptr;
        other.m_slots = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;

        return *this;
    }

    /**
     * @brief 指定した要素数を格納できるよう容量を確保する
     * @param count 要素数
     */
    void reserve(size_t count)
    {
        const size_t required = (count * 10) / 7 + 1;
        if (required > m_capacity)
            rehash(required);
    }

    /**
     * @brief 全要素を破棄する（容量は維持する）
     */
    void clear()
    {
        destroyAll();

        for (size_t index = 0; index < m_capacity; ++index)
            m_hashes[index] = 0;

        m_size = 0;
    }

    /**
     * @brief 要素を追加する（既に存在する場合は上書きする）
     * @param key キー
     * @param value 値
     * @return V& 追加または上書きした値
     */
    V& insert(const K& key, const V& value)
    {
        if (V* existing = find(key))
        {
            *existing = value;
            return *existing;
        }

        return addNew(Entry{ key, value });
    }

    /**
     * @brief 要素を追加する（既に存在する場合は上書きする）
     * @param key キー
     * @param value 値
     * @return V& 追加または上書きした値
     */
    V& insert(const K& key, V&& value)
    {
        if (V* existing = find(key))
        {
            *existing = std::move(value);
            return *existing;
        }

        return addNew(Entry{ key, std::move(value) });
    }

    /**
     * @brief キーに対応する値を取得する（無ければ既定値で追加する）
     * @param key キー
     * @return V& 対応する値
     */
    V& operator[](const K& key)
    {
        if (V* existing = find(key))
            return *existing;

        return addNew(Entry{ key, V{} });
    }

    /**
     * @brief キーに対応する値を探す
     * @param key キー
     * @return V* 対応する値。無ければnullptr
     */
    V* find(const K& key)
    {
        const size_t index = findIndex(key);
        return index != NPOS ? &m_slots[index].value : nullptr;
    }

    /**
     * @brief キーに対応する値を探す
     * @param key キー
     * @return const V* 対応する値。無ければnullptr
     */
    const V* find(const K& key) const
    {
        const size_t index = findIndex(key);
        return index != NPOS ? &m_slots[index].value : nullptr;
    }

    /**
     * @brief キーが含まれるかを判定する
     * @param key キー
     * @return bool 含まれていればtrue
     */
    bool contains(const K& key) const { return findIndex(key) != NPOS; }

    /**
     * @brief 要素を取り除く
     * @param key キー
     * @return bool 取り除いたらtrue
     */
    bool remove(const K& key)
    {
        size_t index = findIndex(key);
        if (index == NPOS)
            return false;

        m_slots[index].~Entry();

        // 後続の要素を1つずつ手前へ詰めて墓標を残さない
        for (;;)
        {
            const size_t next = (index + 1) & mask();
            if (m_hashes[next] == 0 || probeDistance(next) == 0)
            {
                m_hashes[index] = 0;
                break;
            }

            new (m_slots + index) Entry(std::move(m_slots[next]));
            m_slots[next].~Entry();
            m_hashes[index] = m_hashes[next];
            index = next;
        }

        --m_size;
        return true;
    }

    size_t size() const { return m_size; }
    size_t capacity() const { return m_capacity; }
    bool isEmpty() const { return m_size == 0; }

    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, m_capacity); }
    ConstIterator begin() const { return ConstIterator(this, 0); }
    ConstIterator end() const { return ConstIterator(this, m_capacity); }

private:

    size_t mask() const { return m_capacity - 1; }

    /**
     * @brief キーのハッシュ値を求める（0は空きスロットを表すため必ず非0にする）
     */
    static uint64_t hashOf(const K& key) { return HashFunction{}(key) | static_cast<uint64_t>(1); }

    /**
     * @brief スロットが本来の位置からどれだけ離れているかを求める
     */
    size_t probeDistance(size_t index) const { return (index - (m_hashes[index] & mask())) & mask(); }

    IAllocator& allocator()
    {
        return m_allocator != nullptr ? *m_allocator : MemoryManager::instance().getPersistentAllocator();
    }

    size_t findIndex(const K& key) const
    {
        if (m_capacity == 0)
            return NPOS;

        const uint64_t hash = hashOf(key);
        size_t index = hash & mask();
        size_t distance = 0;

        for (;;)
        {
            if (m_hashes[index] == 0 || probeDistance(index) < distance)
                return NPOS;

            if (m_hashes[index] == hash && m_slots[index].key == key)
                return index;

            index = (index + 1) & mask();
            ++distance;
        }
    }

    V& addNew(Entry&& entry)
    {
        growIfNeeded();

        const uint64_t insertedHash = hashOf(entry.key);
        const size_t insertedIndex = placeEntry(insertedHash, std::move(entry));

        ++m_size;
        return m_slots[insertedIndex].value;
    }

    /**
     * @brief 空きスロットへ要素を配置する（ロビンフッド法）
     * @return size_t 最初に渡した要素が最終的に収まった位置
     */
    size_t placeEntry(uint64_t hash, Entry&& entry)
    {
        size_t index = hash & mask();
        size_t distance = 0;
        size_t result = NPOS;

        for (;;)
        {
            if (m_hashes[index] == 0)
            {
                new (m_slots + index) Entry(std::move(entry));
                m_hashes[index] = hash;
                return result != NPOS ? result : index;
            }

            const size_t existingDistance = probeDistance(index);
            if (existingDistance < distance)
            {
                // 恵まれている要素を押しのけ、その要素を代わりに運ぶ
                std::swap(hash, m_hashes[index]);
                std::swap(entry, m_slots[index]);
                distance = existingDistance;

                if (result == NPOS)
                    result = index;
            }

            index = (index + 1) & mask();
            ++distance;
        }
    }

    void growIfNeeded()
    {
        if (m_capacity == 0)
        {
            rehash(DEFAULT_CAPACITY);
            return;
        }

        // 使用率が70%を超えたら拡張する
        if ((m_size + 1) * 10 >= m_capacity * 7)
            rehash(m_capacity * 2);
    }

    void rehash(size_t requestedCapacity)
    {
        size_t newCapacity = DEFAULT_CAPACITY;
        while (newCapacity < requestedCapacity)
            newCapacity <<= 1;

        uint64_t* oldHashes = m_hashes;
        Entry* oldSlots = m_slots;
        const size_t oldCapacity = m_capacity;

        memorySetSource(nullptr, 0, m_tag);
        void* hashMemory = memoryAllocateFrom(allocator(), sizeof(uint64_t) * newCapacity, alignof(uint64_t));

        memorySetSource(nullptr, 0, m_tag);
        void* slotMemory = memoryAllocateFrom(allocator(), sizeof(Entry) * newCapacity, memoryAlignmentOf<Entry>());

        GE_ASSERT_MSG(hashMemory != nullptr && slotMemory != nullptr, "ハッシュマップの確保に失敗しました ({} 要素)", newCapacity);

        if (hashMemory == nullptr || slotMemory == nullptr)
        {
            memoryFree(hashMemory);
            memoryFree(slotMemory);
            return;
        }

        m_hashes = static_cast<uint64_t*>(hashMemory);
        m_slots = static_cast<Entry*>(slotMemory);
        m_capacity = newCapacity;

        for (size_t index = 0; index < newCapacity; ++index)
            m_hashes[index] = 0;

        for (size_t index = 0; index < oldCapacity; ++index)
        {
            if (oldHashes[index] == 0)
                continue;

            placeEntry(oldHashes[index], std::move(oldSlots[index]));
            oldSlots[index].~Entry();
        }

        memoryFree(oldHashes);
        memoryFree(oldSlots);
    }

    void destroyAll()
    {
        if constexpr (!std::is_trivially_destructible_v<Entry>)
        {
            for (size_t index = 0; index < m_capacity; ++index)
            {
                if (m_hashes[index] != 0)
                    m_slots[index].~Entry();
            }
        }
    }

    void releaseStorage()
    {
        memoryFree(m_hashes);
        memoryFree(m_slots);

        m_hashes = nullptr;
        m_slots = nullptr;
        m_capacity = 0;
    }

    uint64_t*   m_hashes = nullptr;             //!< 各スロットのハッシュ値（0は空き）
    Entry*      m_slots = nullptr;              //!< 各スロットの要素
    size_t      m_capacity = 0;                 //!< スロット数（2の累乗）
    size_t      m_size = 0;                     //!< 要素数
    IAllocator* m_allocator = nullptr;          //!< 確保元のアロケータ
    MemoryTag   m_tag = MemoryTag::CONTAINER;   //!< 用途タグ
};

} // namespace Engine
