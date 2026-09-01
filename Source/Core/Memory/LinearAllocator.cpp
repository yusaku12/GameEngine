#include "Pch.h"
#include "Core\Memory\LinearAllocator.h"
#include "Core\Memory\MemoryTracker.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{
    LinearAllocator::~LinearAllocator()
    {
        finalize();
    }

    bool LinearAllocator::initialize(void* memory, size_t size, const char* name)
    {
        GE_MEMORY_ASSERT(memory != nullptr, "管理対象のメモリがnullptrです");
        GE_MEMORY_ASSERT(m_start == nullptr, "初期化済みのアロケータです");

        if (memory == nullptr || size == 0)
            return false;

        m_start = static_cast<unsigned char*>(memory);
        m_capacity = size;
        m_offset = 0;

        m_stats = MemoryStats{};
        m_stats.capacity = size;

        memoryCopyName(m_name, MEMORY_NAME_LENGTH, name);
        return true;
    }

    void LinearAllocator::finalize()
    {
        m_start = nullptr;
        m_capacity = 0;
        m_offset = 0;
        m_stats = MemoryStats{};
    }

    void* LinearAllocator::allocate(size_t size, size_t alignment)
    {
        GE_MEMORY_ASSERT(m_start != nullptr, "初期化されていないアロケータです");
        GE_MEMORY_ASSERT(memoryIsPowerOfTwo(alignment), "アライメントが2の累乗ではありません");

        if (m_start == nullptr || size == 0 || !memoryIsPowerOfTwo(alignment))
            return nullptr;

        const size_t adjustment = memoryAlignAdjustment(m_start + m_offset, alignment);
        if (m_offset + adjustment + size > m_capacity)
            return nullptr;

        unsigned char* result = m_start + m_offset + adjustment;
        m_offset += adjustment + size;

        m_stats.used = m_offset;
        m_stats.peak = m_stats.peak < m_offset ? m_offset : m_stats.peak;
        ++m_stats.allocationCount;
        ++m_stats.allocationTotal;

        return result;
    }

    void LinearAllocator::deallocate(void*)
    {
    }

    bool LinearAllocator::owns(const void* pointer) const
    {
        const unsigned char* target = static_cast<const unsigned char*>(pointer);
        return target >= m_start && target < m_start + m_capacity;
    }

    void LinearAllocator::reset()
    {
#if GE_MEMORY_TRACKING
        if (m_start != nullptr)
            MemoryTracker::instance().forgetRange(m_start, m_capacity);
#endif

        m_offset = 0;
        m_stats.used = 0;
        m_stats.allocationCount = 0;
    }
} // namespace Engine