#include "Pch.h"

#include "Core\Memory\StackAllocator.h"
#include "Core\Memory\MemoryTracker.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{
    StackAllocator::~StackAllocator()
    {
        finalize();
    }

    bool StackAllocator::initialize(void* memory, size_t size, const char* name)
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

    void StackAllocator::finalize()
    {
        m_start = nullptr;
        m_capacity = 0;
        m_offset = 0;
        m_stats = MemoryStats{};
    }

    void* StackAllocator::allocate(size_t size, size_t alignment)
    {
        GE_MEMORY_ASSERT(m_start != nullptr, "初期化されていないアロケータです");
        GE_MEMORY_ASSERT(memoryIsPowerOfTwo(alignment), "アライメントが2の累乗ではありません");

        if (m_start == nullptr || size == 0 || !memoryIsPowerOfTwo(alignment))
            return nullptr;

        const size_t adjustment = memoryAlignAdjustmentWithHeader(m_start + m_offset, alignment, sizeof(StackHeader));
        if (m_offset + adjustment + size > m_capacity)
            return nullptr;

        unsigned char* result = m_start + m_offset + adjustment;

        StackHeader* header = reinterpret_cast<StackHeader*>(result) - 1;
        header->adjustment = static_cast<uint32_t>(adjustment);

        m_offset += adjustment + size;

        m_stats.used = m_offset;
        m_stats.peak = m_stats.peak < m_offset ? m_offset : m_stats.peak;
        ++m_stats.allocationCount;
        ++m_stats.allocationTotal;

        return result;
    }

    void StackAllocator::deallocate(void* pointer)
    {
        if (pointer == nullptr)
            return;

        GE_MEMORY_ASSERT(owns(pointer), "このアロケータが管理していないポインタです");

        const StackHeader* header = static_cast<const StackHeader*>(pointer) - 1;
        const size_t blockOffset = memoryPointerDistance(pointer, m_start) - header->adjustment;

        GE_MEMORY_ASSERT(blockOffset < m_offset, "確保と逆順で解放されていません");

        m_offset = blockOffset;
        m_stats.used = m_offset;

        if (m_stats.allocationCount > 0)
            --m_stats.allocationCount;
    }

    bool StackAllocator::owns(const void* pointer) const
    {
        const unsigned char* target = static_cast<const unsigned char*>(pointer);
        return target >= m_start && target < m_start + m_capacity;
    }

    void StackAllocator::freeToMarker(MemoryMarker marker)
    {
        if (marker >= m_offset)
            return;

#if GE_MEMORY_TRACKING
        MemoryTracker::instance().forgetRange(m_start + marker, m_offset - marker);
#endif

        m_offset = marker;
        m_stats.used = m_offset;
    }

    void StackAllocator::reset()
    {
        freeToMarker(0);
        m_stats.allocationCount = 0;
    }
} // namespace Engine