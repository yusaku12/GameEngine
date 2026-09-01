#include "Pch.h"
#include "Core\Memory\MemoryApi.h"
#include "Core\Memory\MemoryTracker.h"

namespace Engine
{
    //! ヘッダの正当性を確認するためのシグネチャ
    static constexpr uint8_t MEMORY_HEADER_SIGNATURE = static_cast<uint8_t>(0x5A);

    /**
     * @brief 確保済みメモリの管理情報（ユーザ領域の直前に配置する）
     */
    struct MemoryAllocationHeader
    {
        IAllocator* allocator;   //!< 確保元のアロケータ
        uint32_t    size;        //!< ユーザが要求したサイズ
        uint16_t    headerSpace; //!< ユーザ領域から生ブロック先頭までの距離
        uint8_t     tag;         //!< 用途タグ
        uint8_t     signature;   //!< 破壊検出用のシグネチャ
    };

    static_assert(sizeof(MemoryAllocationHeader) == 16, "MemoryAllocationHeaderは16バイトである必要があります");

    static thread_local const char* s_sourceFile = nullptr;              //!< 次の確保に付与するファイル名
    static thread_local int         s_sourceLine = 0;                    //!< 次の確保に付与する行番号
    static thread_local MemoryTag   s_sourceTag = MemoryTag::UNKNOWN;    //!< 次の確保に付与する用途タグ
    static bool                     s_fillPatternEnabled = GE_MEMORY_TRACKING != 0; //!< フィルパターンの有効/無効

    /**
     * @brief ユーザ領域の直前にあるヘッダを取得する
     * @param pointer ユーザ領域のポインタ
     * @return MemoryAllocationHeader* ヘッダ
     */
    static MemoryAllocationHeader* memoryHeaderOf(const void* pointer)
    {
        return const_cast<MemoryAllocationHeader*>(static_cast<const MemoryAllocationHeader*>(pointer) - 1);
    }

    /**
     * @brief ヘッダが正しいかを検証する
     * @param header 検証するヘッダ
     * @return bool 正しければtrue
     */
    static bool memoryValidateHeader(const MemoryAllocationHeader* header)
    {
        return header != nullptr && header->signature == MEMORY_HEADER_SIGNATURE && header->allocator != nullptr;
    }

    const char* memoryTagName(MemoryTag tag)
    {
        switch (tag)
        {
        case MemoryTag::UNKNOWN:    return "Unknown";
        case MemoryTag::SYSTEM:     return "System";
        case MemoryTag::GRAPHICS:   return "Graphics";
        case MemoryTag::RESOURCE:   return "Resource";
        case MemoryTag::AUDIO:      return "Audio";
        case MemoryTag::SCENE:      return "Scene";
        case MemoryTag::FRAME:      return "Frame";
        case MemoryTag::CONTAINER:  return "Container";
        case MemoryTag::DEBUG_INFO: return "Debug";
        default:                    return "Invalid";
        }
    }

    void memorySetSource(const char* file, int line, MemoryTag tag)
    {
        s_sourceFile = file;
        s_sourceLine = line;
        s_sourceTag = tag;
    }

    void memorySetFillPatternEnabled(bool enabled)
    {
        s_fillPatternEnabled = enabled;
    }

    void* memoryAllocateFrom(IAllocator& allocator, size_t size, size_t alignment)
    {
        const char* sourceFile = s_sourceFile;
        const int sourceLine = s_sourceLine;
        const MemoryTag tag = s_sourceTag;

        // 呼び出し元情報は1回の確保でのみ有効にする
        memorySetSource(nullptr, 0, MemoryTag::UNKNOWN);

        GE_MEMORY_ASSERT(memoryIsPowerOfTwo(alignment), "アライメントが2の累乗ではありません");

        if (size == 0 || !memoryIsPowerOfTwo(alignment))
            return nullptr;

        if (alignment < alignof(MemoryAllocationHeader))
            alignment = alignof(MemoryAllocationHeader);

        const size_t headerSpace = memoryAlignUp(sizeof(MemoryAllocationHeader), alignment);
        const size_t guardSize = GE_MEMORY_TRACKING != 0 ? MEMORY_GUARD_SIZE : 0;

        void* raw = allocator.allocate(headerSpace + size + guardSize, alignment);
        if (raw == nullptr)
        {
            LOG_ERROR("[Memory] メモリ確保に失敗しました ({} バイト / allocator={} / tag={})",
                size,
                allocator.getName(),
                memoryTagName(tag));
            return nullptr;
        }

        unsigned char* result = static_cast<unsigned char*>(raw) + headerSpace;

        MemoryAllocationHeader* header = memoryHeaderOf(result);
        header->allocator = &allocator;
        header->size = static_cast<uint32_t>(size);
        header->headerSpace = static_cast<uint16_t>(headerSpace);
        header->tag = static_cast<uint8_t>(tag);
        header->signature = MEMORY_HEADER_SIGNATURE;

#if GE_MEMORY_TRACKING
        if (s_fillPatternEnabled)
            std::memset(result, MEMORY_ALLOCATED_PATTERN, size);

        std::memset(result + size, MEMORY_GUARD_PATTERN, MEMORY_GUARD_SIZE);

        MemoryTracker::instance().recordAllocate(result, size, tag, allocator.getName(), sourceFile, sourceLine);
#else
        (void)sourceFile;
        (void)sourceLine;
#endif

        return result;
    }

    void* memoryAllocate(size_t size, size_t alignment)
    {
        MemoryManager& manager = MemoryManager::instance();
        GE_MEMORY_ASSERT(manager.isInitialized(), "MemoryManagerが初期化されていません");

        return memoryAllocateFrom(manager.getPersistentAllocator(), size, alignment);
    }

    void* memoryAllocateFrame(size_t size, size_t alignment)
    {
        MemoryManager& manager = MemoryManager::instance();
        GE_MEMORY_ASSERT(manager.isInitialized(), "MemoryManagerが初期化されていません");

        return memoryAllocateFrom(manager.getFrameAllocator(), size, alignment);
    }

    void* memoryReallocate(void* pointer, size_t size, size_t alignment)
    {
        if (pointer == nullptr)
            return memoryAllocate(size, alignment);

        MemoryAllocationHeader* header = memoryHeaderOf(pointer);
        if (!memoryValidateHeader(header))
        {
            LOG_ERROR("[Memory] 不正なポインタが再確保に渡されました (address=0x{:X})", reinterpret_cast<uintptr_t>(pointer));
            return nullptr;
        }

        if (size == 0)
        {
            memoryFree(pointer);
            return nullptr;
        }

        const size_t oldSize = header->size;
        if (oldSize == size)
            return pointer;

        IAllocator& allocator = *header->allocator;
        const MemoryTag tag = static_cast<MemoryTag>(header->tag);

        memorySetSource(s_sourceFile, s_sourceLine, tag);
        void* result = memoryAllocateFrom(allocator, size, alignment);
        if (result == nullptr)
            return nullptr;

        std::memcpy(result, pointer, oldSize < size ? oldSize : size);
        memoryFree(pointer);

        return result;
    }

    void memoryFree(void* pointer)
    {
        if (pointer == nullptr)
            return;

        MemoryAllocationHeader* header = memoryHeaderOf(pointer);
        if (!memoryValidateHeader(header))
        {
            LOG_ERROR("[Memory] 不正なポインタ、または二重解放を検出しました (address=0x{:X})", reinterpret_cast<uintptr_t>(pointer));
            return;
        }

        IAllocator* allocator = header->allocator;
        [[maybe_unused]] const size_t size = header->size;
        const size_t headerSpace = header->headerSpace;

#if GE_MEMORY_TRACKING
        const unsigned char* guard = static_cast<const unsigned char*>(pointer) + size;
        for (size_t index = 0; index < MEMORY_GUARD_SIZE; ++index)
        {
            if (guard[index] != MEMORY_GUARD_PATTERN)
            {
                LOG_ERROR("[Memory] バッファオーバーランを検出しました (address=0x{:X} / {} バイト / tag={})",
                    reinterpret_cast<uintptr_t>(pointer),
                    size,
                    memoryTagName(static_cast<MemoryTag>(header->tag)));
                break;
            }
        }

        MemoryTracker::instance().recordDeallocate(pointer);

        if (s_fillPatternEnabled)
            std::memset(pointer, MEMORY_FREED_PATTERN, size);
#endif

        header->signature = 0;

        void* raw = static_cast<unsigned char*>(pointer) - headerSpace;
        allocator->deallocate(raw);
    }

    size_t memorySizeOf(const void* pointer)
    {
        if (pointer == nullptr)
            return 0;

        const MemoryAllocationHeader* header = memoryHeaderOf(pointer);
        return memoryValidateHeader(header) ? header->size : 0;
    }

    MemoryTag memoryTagOf(const void* pointer)
    {
        if (pointer == nullptr)
            return MemoryTag::UNKNOWN;

        const MemoryAllocationHeader* header = memoryHeaderOf(pointer);
        return memoryValidateHeader(header) ? static_cast<MemoryTag>(header->tag) : MemoryTag::UNKNOWN;
    }

    IAllocator* memoryAllocatorOf(const void* pointer)
    {
        if (pointer == nullptr)
            return nullptr;

        const MemoryAllocationHeader* header = memoryHeaderOf(pointer);
        return memoryValidateHeader(header) ? header->allocator : nullptr;
    }
} // namespace Engine