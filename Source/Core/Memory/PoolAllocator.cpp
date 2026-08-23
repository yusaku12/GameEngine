#include "Pch.h"

#include "Core\Memory\PoolAllocator.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{
    PoolAllocator::~PoolAllocator()
    {
        finalize();
    }

    bool PoolAllocator::initialize(void* memory, size_t size, size_t blockSize, size_t alignment, const char* name)
    {
        GE_MEMORY_ASSERT(memory != nullptr, "管理対象のメモリがnullptrです");
        GE_MEMORY_ASSERT(memoryIsPowerOfTwo(alignment), "アライメントが2の累乗ではありません");

        if (memory == nullptr || size == 0 || blockSize == 0 || !memoryIsPowerOfTwo(alignment))
            return false;

        // フリーリストのポインタを格納するため、最低でもポインタサイズは必要になる
        const size_t requestedSize = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;

        m_base = memory;
        m_alignment = alignment;
        m_blockSize = memoryAlignUp(requestedSize, alignment);

        const size_t adjustment = memoryAlignAdjustment(memory, alignment);
        if (adjustment >= size)
            return false;

        m_start = static_cast<unsigned char*>(memory) + adjustment;
        m_blockCount = (size - adjustment) / m_blockSize;

        if (m_blockCount == 0)
        {
            finalize();
            return false;
        }

        // 全ブロックを未使用として連結する
        m_freeList = reinterpret_cast<void**>(m_start);
        void** current = m_freeList;
        for (size_t index = 1; index < m_blockCount; ++index)
        {
            void** next = reinterpret_cast<void**>(m_start + index * m_blockSize);
            *current = next;
            current = next;
        }
        *current = nullptr;

        m_stats = MemoryStats{};
        m_stats.capacity = m_blockCount * m_blockSize;

        memoryCopyName(m_name, MEMORY_NAME_LENGTH, name);
        return true;
    }

    void PoolAllocator::finalize()
    {
        m_base = nullptr;
        m_start = nullptr;
        m_blockSize = 0;
        m_blockCount = 0;
        m_alignment = 0;
        m_freeList = nullptr;
        m_stats = MemoryStats{};
    }

    void* PoolAllocator::allocate(size_t size, [[maybe_unused]] size_t alignment)
    {
        GE_MEMORY_ASSERT(m_start != nullptr, "初期化されていないアロケータです");
        GE_MEMORY_ASSERT(size <= m_blockSize, "ブロックサイズを超える確保要求です");
        GE_MEMORY_ASSERT(alignment <= m_alignment, "ブロックのアライメントを超える確保要求です");

        if (m_freeList == nullptr || size > m_blockSize || alignment > m_alignment)
            return nullptr;

        void** result = m_freeList;
        m_freeList = static_cast<void**>(*m_freeList);

        m_stats.used += m_blockSize;
        m_stats.peak = m_stats.peak < m_stats.used ? m_stats.used : m_stats.peak;
        ++m_stats.allocationCount;
        ++m_stats.allocationTotal;

        return result;
    }

    void PoolAllocator::deallocate(void* pointer)
    {
        if (pointer == nullptr)
            return;

        GE_MEMORY_ASSERT(owns(pointer), "このアロケータが管理していないポインタです");
        GE_MEMORY_ASSERT(memoryPointerDistance(pointer, m_start) % m_blockSize == 0, "ブロック先頭ではないポインタです");

        if (!owns(pointer))
            return;

        void** block = static_cast<void**>(pointer);
        *block = m_freeList;
        m_freeList = block;

        m_stats.used -= m_blockSize;
        --m_stats.allocationCount;
    }

    bool PoolAllocator::owns(const void* pointer) const
    {
        const unsigned char* target = static_cast<const unsigned char*>(pointer);
        return target >= m_start && target < m_start + m_blockCount * m_blockSize;
    }
} // namespace Engine