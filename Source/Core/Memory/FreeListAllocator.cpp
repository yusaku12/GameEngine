#include "Pch.h"

#include "Core\Memory\FreeListAllocator.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{
    FreeListAllocator::~FreeListAllocator()
    {
        finalize();
    }

    bool FreeListAllocator::initialize(void* memory, size_t size, const char* name)
    {
        GE_MEMORY_ASSERT(memory != nullptr, "管理対象のメモリがnullptrです");
        GE_MEMORY_ASSERT(m_start == nullptr, "初期化済みのアロケータです");

        if (memory == nullptr || size < sizeof(FreeBlock))
            return false;

        m_start = static_cast<unsigned char*>(memory);
        m_capacity = size;

        m_freeList = reinterpret_cast<FreeBlock*>(m_start);
        m_freeList->size = size;
        m_freeList->next = nullptr;

        m_stats = MemoryStats{};
        m_stats.capacity = size;

        memoryCopyName(m_name, MEMORY_NAME_LENGTH, name);
        return true;
    }

    void FreeListAllocator::finalize()
    {
        m_start = nullptr;
        m_capacity = 0;
        m_freeList = nullptr;
        m_stats = MemoryStats{};
    }

    void* FreeListAllocator::allocate(size_t size, size_t alignment)
    {
        GE_MEMORY_ASSERT(m_start != nullptr, "初期化されていないアロケータです");
        GE_MEMORY_ASSERT(memoryIsPowerOfTwo(alignment), "アライメントが2の累乗ではありません");

        if (m_start == nullptr || size == 0 || !memoryIsPowerOfTwo(alignment))
            return nullptr;

        if (alignment < alignof(BlockHeader))
            alignment = alignof(BlockHeader);

        // 最良適合の空きブロックを探す
        FreeBlock* previous = nullptr;
        FreeBlock* current = m_freeList;
        FreeBlock* bestPrevious = nullptr;
        FreeBlock* best = nullptr;
        size_t bestAdjustment = 0;
        size_t bestTotal = 0;

        while (current != nullptr)
        {
            const size_t adjustment = memoryAlignAdjustmentWithHeader(current, alignment, sizeof(BlockHeader));
            const size_t total = adjustment + size;

            if (current->size >= total && (best == nullptr || current->size < best->size))
            {
                bestPrevious = previous;
                best = current;
                bestAdjustment = adjustment;
                bestTotal = total;

                if (current->size == total)
                    break;
            }

            previous = current;
            current = current->next;
        }

        if (best == nullptr)
            return nullptr;

        // 余りが空きブロックとして成立しない場合はブロックごと使い切る
        const size_t remaining = best->size - bestTotal;
        if (remaining < sizeof(FreeBlock))
        {
            bestTotal = best->size;

            if (bestPrevious != nullptr)
                bestPrevious->next = best->next;
            else
                m_freeList = best->next;
        }
        else
        {
            FreeBlock* split = reinterpret_cast<FreeBlock*>(reinterpret_cast<unsigned char*>(best) + bestTotal);
            split->size = remaining;
            split->next = best->next;

            if (bestPrevious != nullptr)
                bestPrevious->next = split;
            else
                m_freeList = split;
        }

        unsigned char* result = reinterpret_cast<unsigned char*>(best) + bestAdjustment;

        BlockHeader* header = reinterpret_cast<BlockHeader*>(result) - 1;
        header->size = bestTotal;
        header->adjustment = static_cast<uint32_t>(bestAdjustment);
        header->reserved = 0;

        m_stats.used += bestTotal;
        m_stats.peak = m_stats.peak < m_stats.used ? m_stats.used : m_stats.peak;
        ++m_stats.allocationCount;
        ++m_stats.allocationTotal;

        return result;
    }

    void FreeListAllocator::deallocate(void* pointer)
    {
        if (pointer == nullptr)
            return;

        GE_MEMORY_ASSERT(owns(pointer), "このアロケータが管理していないポインタです");

        if (!owns(pointer))
            return;

        const BlockHeader* header = static_cast<const BlockHeader*>(pointer) - 1;
        const size_t blockSize = header->size;
        const size_t adjustment = header->adjustment;

        unsigned char* blockStart = static_cast<unsigned char*>(pointer) - adjustment;

        // ヘッダを上書きするため、必要な値を取り出してから空きブロックを構築する
        FreeBlock* block = reinterpret_cast<FreeBlock*>(blockStart);
        block->size = blockSize;
        block->next = nullptr;
        insertFreeBlock(block);

        m_stats.used -= blockSize;
        --m_stats.allocationCount;
    }

    void FreeListAllocator::insertFreeBlock(FreeBlock* block)
    {
        FreeBlock* previous = nullptr;
        FreeBlock* current = m_freeList;

        while (current != nullptr && current < block)
        {
            previous = current;
            current = current->next;
        }

        block->next = current;

        if (previous != nullptr)
            previous->next = block;
        else
            m_freeList = block;

        // 後方のブロックと結合する
        if (current != nullptr && reinterpret_cast<unsigned char*>(block) + block->size == reinterpret_cast<unsigned char*>(current))
        {
            block->size += current->size;
            block->next = current->next;
        }

        // 前方のブロックと結合する
        if (previous != nullptr && reinterpret_cast<unsigned char*>(previous) + previous->size == reinterpret_cast<unsigned char*>(block))
        {
            previous->size += block->size;
            previous->next = block->next;
        }
    }

    bool FreeListAllocator::owns(const void* pointer) const
    {
        const unsigned char* target = static_cast<const unsigned char*>(pointer);
        return target >= m_start && target < m_start + m_capacity;
    }

    size_t FreeListAllocator::getLargestFreeBlock() const
    {
        size_t largest = 0;
        for (const FreeBlock* block = m_freeList; block != nullptr; block = block->next)
        {
            if (largest < block->size)
                largest = block->size;
        }
        return largest;
    }

    size_t FreeListAllocator::getFreeBlockCount() const
    {
        size_t count = 0;
        for (const FreeBlock* block = m_freeList; block != nullptr; block = block->next)
            ++count;

        return count;
    }
} // namespace Engine