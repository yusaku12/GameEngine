#include "Pch.h"

#include "Core\Memory\MemoryTracker.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{
    //! 記録表の最小容量
    static constexpr size_t MEMORY_TRACKER_MIN_CAPACITY = static_cast<size_t>(1024);

    /**
     * @brief ポインタからハッシュ値を求める
     * @param pointer 対象のポインタ
     * @return size_t ハッシュ値
     */
    static size_t memoryHashPointer(const void* pointer)
    {
        uint64_t value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
        value >>= 4;
        value *= static_cast<uint64_t>(0x9E3779B97F4A7C15);
        return static_cast<size_t>(value >> 29);
    }

    bool MemoryTracker::initialize(size_t capacity)
    {
        if (m_entries != nullptr)
            return true;

        size_t tableSize = MEMORY_TRACKER_MIN_CAPACITY;
        while (tableSize < capacity)
            tableSize <<= 1;

        m_entries = static_cast<Entry*>(std::calloc(tableSize, sizeof(Entry)));
        if (m_entries == nullptr)
            return false;

        m_capacity = tableSize;
        m_used = 0;
        m_liveCount = 0;
        m_liveSize = 0;
        m_serial = 0;
        m_overflowReported = false;

        for (size_t index = 0; index < static_cast<size_t>(MemoryTag::COUNT); ++index)
            m_tagStats[index] = MemoryStats{};

        return true;
    }

    void MemoryTracker::finalize()
    {
        if (m_entries == nullptr)
            return;

        std::free(m_entries);
        m_entries = nullptr;
        m_capacity = 0;
        m_used = 0;
        m_liveCount = 0;
        m_liveSize = 0;
    }

    void MemoryTracker::recordAllocate(const void* pointer, size_t size, MemoryTag tag, const char* allocatorName, const char* file, int line)
    {
        if (m_entries == nullptr || pointer == nullptr)
            return;

        // 記録表が埋まりすぎると探索が終わらなくなるため、余裕を残して打ち切る
        if (m_used >= m_capacity - (m_capacity >> 2))
        {
            if (!m_overflowReported)
            {
                m_overflowReported = true;
                LOG_WARNING("メモリ追跡表が容量不足です。MemoryConfig::trackerCapacity を増やしてください (capacity={})", m_capacity);
            }
            return;
        }

        const size_t mask = m_capacity - 1;
        size_t index = memoryHashPointer(pointer) & mask;

        while (m_entries[index].state == SlotState::OCCUPIED)
        {
            if (m_entries[index].pointer == pointer)
            {
                LOG_ERROR("同じアドレスが二重に確保されました (address=0x{:X})", reinterpret_cast<uintptr_t>(pointer));
                return;
            }
            index = (index + 1) & mask;
        }

        if (m_entries[index].state == SlotState::EMPTY)
            ++m_used;

        Entry& entry = m_entries[index];
        entry.pointer = pointer;
        entry.size = size;
        entry.file = file;
        entry.allocatorName = allocatorName;
        entry.line = static_cast<uint32_t>(line);
        entry.serial = ++m_serial;
        entry.tag = tag;
        entry.state = SlotState::OCCUPIED;

        ++m_liveCount;
        m_liveSize += size;

        MemoryStats& stats = m_tagStats[static_cast<size_t>(tag)];
        stats.used += size;
        stats.peak = stats.peak < stats.used ? stats.used : stats.peak;
        ++stats.allocationCount;
        ++stats.allocationTotal;
    }

    void MemoryTracker::recordDeallocate(const void* pointer)
    {
        if (m_entries == nullptr || pointer == nullptr)
            return;

        Entry* entry = findEntry(pointer);
        if (entry == nullptr)
            return;

        removeEntry(entry);
    }

    void MemoryTracker::forgetRange(const void* begin, size_t size)
    {
        if (m_entries == nullptr || begin == nullptr)
            return;

        const unsigned char* low = static_cast<const unsigned char*>(begin);
        const unsigned char* high = low + size;

        for (size_t index = 0; index < m_capacity; ++index)
        {
            Entry& entry = m_entries[index];
            if (entry.state != SlotState::OCCUPIED)
                continue;

            const unsigned char* target = static_cast<const unsigned char*>(entry.pointer);
            if (target >= low && target < high)
                removeEntry(&entry);
        }
    }

    MemoryTracker::Entry* MemoryTracker::findEntry(const void* pointer) const
    {
        const size_t mask = m_capacity - 1;
        size_t index = memoryHashPointer(pointer) & mask;

        for (size_t step = 0; step < m_capacity; ++step)
        {
            Entry& entry = m_entries[index];
            if (entry.state == SlotState::EMPTY)
                return nullptr;

            if (entry.state == SlotState::OCCUPIED && entry.pointer == pointer)
                return &entry;

            index = (index + 1) & mask;
        }

        return nullptr;
    }

    void MemoryTracker::removeEntry(Entry* entry)
    {
        MemoryStats& stats = m_tagStats[static_cast<size_t>(entry->tag)];
        stats.used -= entry->size;
        --stats.allocationCount;

        m_liveSize -= entry->size;
        --m_liveCount;

        entry->pointer = nullptr;
        entry->size = 0;
        entry->state = SlotState::DELETED;
    }

    size_t MemoryTracker::dumpLeaks() const
    {
        if (m_entries == nullptr)
            return 0;

        if (m_liveCount == 0)
        {
            LOG_INFO("[Memory] メモリリークはありません");
            return 0;
        }

        LOG_ERROR("[Memory] メモリリークを {} 件検出しました (合計 {} バイト)", m_liveCount, m_liveSize);

        for (size_t index = 0; index < m_capacity; ++index)
        {
            const Entry& entry = m_entries[index];
            if (entry.state != SlotState::OCCUPIED)
                continue;

            LOG_ERROR("[Memory] leak #{} : {} バイト / tag={} / allocator={} / {}({})",
                entry.serial,
                entry.size,
                memoryTagName(entry.tag),
                entry.allocatorName != nullptr ? entry.allocatorName : "unknown",
                entry.file != nullptr ? entry.file : "unknown",
                entry.line);
        }

        return m_liveCount;
    }

    void MemoryTracker::dumpStats() const
    {
        if (m_entries == nullptr)
            return;

        LOG_INFO("[Memory] 用途別使用状況 (合計 {} バイト / {} 件)", m_liveSize, m_liveCount);

        for (size_t index = 0; index < static_cast<size_t>(MemoryTag::COUNT); ++index)
        {
            const MemoryStats& stats = m_tagStats[index];
            if (stats.allocationTotal == 0)
                continue;

            LOG_INFO("[Memory]   {:<10} : {:>10} バイト / ピーク {:>10} バイト / {} 件",
                memoryTagName(static_cast<MemoryTag>(index)),
                stats.used,
                stats.peak,
                stats.allocationCount);
        }
    }

    const MemoryStats& MemoryTracker::getTagStats(MemoryTag tag) const
    {
        const size_t index = static_cast<size_t>(tag);
        return m_tagStats[index < static_cast<size_t>(MemoryTag::COUNT) ? index : 0];
    }
} // namespace Engine