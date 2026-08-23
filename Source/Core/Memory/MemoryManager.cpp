#include "Pch.h"

#include "Core\Memory\MemoryApi.h"
#include "Core\Memory\MemoryManager.h"
#include "Core\Memory\MemoryTracker.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{

bool MemoryManager::initialize(const MemoryConfig& config)
{
    if (m_initialized)
        return true;

    const size_t persistentSize = memoryAlignUp(config.persistentSize, MEMORY_PAGE_ALIGNMENT);
    const size_t frameSize = memoryAlignUp(config.frameSize, MEMORY_PAGE_ALIGNMENT);

    if (persistentSize == 0 || frameSize == 0)
    {
        LOG_ERROR("[Memory] ヒープサイズの指定が不正です");
        return false;
    }

    m_rootSize = persistentSize + frameSize * 2;
    m_rootMemory = ::VirtualAlloc(nullptr, m_rootSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (m_rootMemory == nullptr)
    {
        LOG_ERROR("[Memory] ルートメモリの確保に失敗しました ({} バイト)", m_rootSize);
        m_rootSize = 0;
        return false;
    }

    unsigned char* cursor = static_cast<unsigned char*>(m_rootMemory);

    bool succeeded = m_persistent.initialize(cursor, persistentSize, "Persistent");
    cursor += persistentSize;

    succeeded = m_frame[0].initialize(cursor, frameSize, "Frame0") && succeeded;
    cursor += frameSize;

    succeeded = m_frame[1].initialize(cursor, frameSize, "Frame1") && succeeded;

    if (!succeeded)
    {
        LOG_ERROR("[Memory] アロケータの初期化に失敗しました");
        ::VirtualFree(m_rootMemory, 0, MEM_RELEASE);
        m_rootMemory = nullptr;
        m_rootSize = 0;
        return false;
    }

    m_frameIndex = 0;
    m_frameCount = 0;
    m_initialized = true;

    memorySetFillPatternEnabled(config.enableFillPattern);

#if GE_MEMORY_TRACKING
    if (config.enableTracking && !MemoryTracker::instance().initialize(config.trackerCapacity))
        LOG_WARNING("[Memory] メモリ追跡機能の初期化に失敗しました");
#endif

    LOG_INFO("[Memory] メモリシステムを初期化しました (合計 {} MiB / 永続 {} MiB / フレーム {} MiB x2 / 追跡 {})",
        m_rootSize / MEMORY_MIB,
        persistentSize / MEMORY_MIB,
        frameSize / MEMORY_MIB,
        MemoryTracker::instance().isEnabled() ? "有効" : "無効");

    return true;
}

size_t MemoryManager::finalize()
{
    if (!m_initialized)
        return 0;

    size_t leakCount = 0;

#if GE_MEMORY_TRACKING
    // フレームヒープの内容は寿命どおりに破棄されるため、リーク判定から除外する
    m_frame[0].reset();
    m_frame[1].reset();

    leakCount = MemoryTracker::instance().dumpLeaks();
    MemoryTracker::instance().finalize();
#endif

    if (m_persistent.getStats().allocationCount != 0)
    {
        LOG_WARNING("[Memory] 永続ヒープに未解放の割り当てが {} 件残っています ({} バイト)",
            m_persistent.getStats().allocationCount,
            m_persistent.getStats().used);
    }

    m_frame[1].finalize();
    m_frame[0].finalize();
    m_persistent.finalize();

    ::VirtualFree(m_rootMemory, 0, MEM_RELEASE);
    m_rootMemory = nullptr;
    m_rootSize = 0;
    m_frameIndex = 0;
    m_initialized = false;

    LOG_INFO("[Memory] メモリシステムを終了しました (リーク {} 件)", leakCount);

    return leakCount;
}

void MemoryManager::beginFrame()
{
    if (!m_initialized)
        return;

    ++m_frameCount;
    m_frameIndex ^= 1;
    m_frame[m_frameIndex].reset();
}

bool MemoryManager::createPool(PoolAllocator& pool, size_t blockSize, size_t blockCount, size_t alignment, const char* name)
{
    GE_MEMORY_ASSERT(m_initialized, "MemoryManagerが初期化されていません");

    if (!m_initialized || blockSize == 0 || blockCount == 0 || !memoryIsPowerOfTwo(alignment))
        return false;

    const size_t requestedSize = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;

    // アライメント済みのメモリを渡すため、ブロック数ぴったりの大きさで確保する
    const size_t poolSize = memoryAlignUp(requestedSize, alignment) * blockCount;

    void* memory = m_persistent.allocate(poolSize, alignment);
    if (memory == nullptr)
    {
        LOG_ERROR("[Memory] プール '{}' 用のメモリ確保に失敗しました ({} バイト)", name != nullptr ? name : "", poolSize);
        return false;
    }

    if (!pool.initialize(memory, poolSize, blockSize, alignment, name))
    {
        m_persistent.deallocate(memory);
        return false;
    }

    LOG_INFO("[Memory] プール '{}' を作成しました ({} バイト x {} ブロック)", pool.getName(), pool.getBlockSize(), pool.getBlockCount());
    return true;
}

void MemoryManager::destroyPool(PoolAllocator& pool)
{
    void* memory = pool.getBaseMemory();
    if (memory == nullptr)
        return;

    if (pool.getStats().allocationCount != 0)
    {
        LOG_WARNING("[Memory] プール '{}' に未解放のブロックが {} 件残っています",
            pool.getName(),
            pool.getStats().allocationCount);
    }

    pool.finalize();
    m_persistent.deallocate(memory);
}

MemoryStats MemoryManager::getStats() const
{
    MemoryStats stats{};
    stats.capacity = m_rootSize;

    const MemoryStats& persistent = m_persistent.getStats();
    const MemoryStats& frame0 = m_frame[0].getStats();
    const MemoryStats& frame1 = m_frame[1].getStats();

    stats.used = persistent.used + frame0.used + frame1.used;
    stats.peak = persistent.peak + frame0.peak + frame1.peak;
    stats.allocationCount = persistent.allocationCount + frame0.allocationCount + frame1.allocationCount;
    stats.allocationTotal = persistent.allocationTotal + frame0.allocationTotal + frame1.allocationTotal;

    return stats;
}

void MemoryManager::dumpStats() const
{
    if (!m_initialized)
    {
        LOG_WARNING("[Memory] メモリシステムが初期化されていません");
        return;
    }

    const MemoryStats& persistent = m_persistent.getStats();

    LOG_INFO("[Memory] --- 使用状況 (frame={}) ---", m_frameCount);
    LOG_INFO("[Memory] 永続  : {} / {} バイト (ピーク {} / 割り当て {} 件 / 空きブロック {} / 最大連続 {} バイト)",
        persistent.used,
        persistent.capacity,
        persistent.peak,
        persistent.allocationCount,
        m_persistent.getFreeBlockCount(),
        m_persistent.getLargestFreeBlock());

    for (size_t index = 0; index < 2; ++index)
    {
        const MemoryStats& frame = m_frame[index].getStats();
        LOG_INFO("[Memory] {} : {} / {} バイト (ピーク {} / 割り当て {} 件)",
            m_frame[index].getName(),
            frame.used,
            frame.capacity,
            frame.peak,
            frame.allocationCount);
    }

#if GE_MEMORY_TRACKING
    MemoryTracker::instance().dumpStats();
#endif
}

} // namespace Engine
