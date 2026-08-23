#pragma once

#include "Core\Memory\IAllocator.h"
#include "Core\Memory\MemoryManager.h"
#include "Core\Memory\MemoryTypes.h"
#include "Core\Memory\MemoryUtility.h"

namespace Engine
{

/**
 * @brief 次の確保に付与する呼び出し元情報を設定する
 * GE_NEWなどのマクロが確保の直前に呼び出す
 * @param file 呼び出し元ファイル
 * @param line 呼び出し元行
 * @param tag 用途タグ
 */
void memorySetSource(const char* file, int line, MemoryTag tag);

/**
 * @brief フィルパターンの書き込みを切り替える
 * @param enabled 有効にするならtrue
 */
void memorySetFillPatternEnabled(bool enabled);

/**
 * @brief 指定したアロケータからメモリを確保する
 * @param allocator 確保元のアロケータ
 * @param size 確保するサイズ（バイト）
 * @param alignment アライメント（2の累乗であること）
 * @return void* 確保したメモリ。失敗した場合はnullptr
 */
void* memoryAllocateFrom(IAllocator& allocator, size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT);

/**
 * @brief 永続ヒープからメモリを確保する
 * @param size 確保するサイズ（バイト）
 * @param alignment アライメント（2の累乗であること）
 * @return void* 確保したメモリ。失敗した場合はnullptr
 */
void* memoryAllocate(size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT);

/**
 * @brief フレームヒープからメモリを確保する
 * 確保したメモリは次のbeginFrame()の2回後に無効になる
 * @param size 確保するサイズ（バイト）
 * @param alignment アライメント（2の累乗であること）
 * @return void* 確保したメモリ。失敗した場合はnullptr
 */
void* memoryAllocateFrame(size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT);

/**
 * @brief 確保済みメモリのサイズを変更する
 * @param pointer 変更するメモリ。nullptrなら新規確保と同じ動作になる
 * @param size 新しいサイズ（バイト）
 * @param alignment アライメント（2の累乗であること）
 * @return void* 変更後のメモリ。失敗した場合はnullptr（元のメモリは保持される）
 */
void* memoryReallocate(void* pointer, size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT);

/**
 * @brief メモリを解放する
 * 確保元のアロケータは内部のヘッダから自動で判別する
 * @param pointer 解放するメモリ
 */
void memoryFree(void* pointer);

/**
 * @brief 確保済みメモリの要求サイズを取得する
 * @param pointer 対象のメモリ
 * @return size_t 要求サイズ（バイト）
 */
size_t memorySizeOf(const void* pointer);

/**
 * @brief 確保済みメモリの用途タグを取得する
 * @param pointer 対象のメモリ
 * @return MemoryTag 用途タグ
 */
MemoryTag memoryTagOf(const void* pointer);

/**
 * @brief 確保済みメモリの確保元アロケータを取得する
 * @param pointer 対象のメモリ
 * @return IAllocator* 確保元のアロケータ。判別できない場合はnullptr
 */
IAllocator* memoryAllocatorOf(const void* pointer);

/**
 * @brief 指定したアロケータ上にオブジェクトを生成する
 * @param allocator 確保元のアロケータ
 * @param args コンストラクタ引数
 * @return T* 生成したオブジェクト。失敗した場合はnullptr
 */
template <class T, class... Args>
T* memoryNewFrom(IAllocator& allocator, Args&&... args)
{
    void* memory = memoryAllocateFrom(allocator, sizeof(T), memoryAlignmentOf<T>());
    if (memory == nullptr)
        return nullptr;

    return new (memory) T(std::forward<Args>(args)...);
}

/**
 * @brief 永続ヒープ上にオブジェクトを生成する
 * @param args コンストラクタ引数
 * @return T* 生成したオブジェクト。失敗した場合はnullptr
 */
template <class T, class... Args>
T* memoryNew(Args&&... args)
{
    return memoryNewFrom<T>(MemoryManager::instance().getPersistentAllocator(), std::forward<Args>(args)...);
}

/**
 * @brief フレームヒープ上にオブジェクトを生成する
 * @param args コンストラクタ引数
 * @return T* 生成したオブジェクト。失敗した場合はnullptr
 */
template <class T, class... Args>
T* memoryNewFrame(Args&&... args)
{
    return memoryNewFrom<T>(MemoryManager::instance().getFrameAllocator(), std::forward<Args>(args)...);
}

/**
 * @brief オブジェクトを破棄してメモリを解放する
 * @param pointer 破棄するオブジェクト
 */
template <class T>
void memoryDelete(T* pointer)
{
    if (pointer == nullptr)
        return;

    pointer->~T();
    memoryFree(pointer);
}

/**
 * @brief 指定したアロケータ上に配列を生成する
 * @param allocator 確保元のアロケータ
 * @param count 要素数
 * @return T* 生成した配列。失敗した場合はnullptr
 */
template <class T>
T* memoryNewArrayFrom(IAllocator& allocator, size_t count)
{
    if (count == 0)
        return nullptr;

    void* memory = memoryAllocateFrom(allocator, sizeof(T) * count, memoryAlignmentOf<T>());
    if (memory == nullptr)
        return nullptr;

    T* elements = static_cast<T*>(memory);
    for (size_t index = 0; index < count; ++index)
        new (elements + index) T();

    return elements;
}

/**
 * @brief 永続ヒープ上に配列を生成する
 * @param count 要素数
 * @return T* 生成した配列。失敗した場合はnullptr
 */
template <class T>
T* memoryNewArray(size_t count)
{
    return memoryNewArrayFrom<T>(MemoryManager::instance().getPersistentAllocator(), count);
}

/**
 * @brief 配列を破棄してメモリを解放する
 * @param pointer 破棄する配列
 */
template <class T>
void memoryDeleteArray(T* pointer)
{
    if (pointer == nullptr)
        return;

    const size_t count = memorySizeOf(pointer) / sizeof(T);
    for (size_t index = count; index > 0; --index)
        pointer[index - 1].~T();

    memoryFree(pointer);
}

/**
 * @brief エンジンのメモリシステムへ返却する削除子
 */
template <class T>
struct MemoryDeleter
{
    void operator()(T* pointer) const noexcept
    {
        memoryDelete(pointer);
    }
};

//! メモリシステム管理下のunique_ptr
template <class T>
using MemoryUniquePtr = std::unique_ptr<T, MemoryDeleter<T>>;

/**
 * @brief 指定したアロケータ上にオブジェクトを生成し、unique_ptrで所有する
 * @param allocator 確保元のアロケータ
 * @param args コンストラクタ引数
 * @return MemoryUniquePtr<T> 生成したオブジェクト
 */
template <class T, class... Args>
MemoryUniquePtr<T> memoryMakeUniqueFrom(IAllocator& allocator, Args&&... args)
{
    return MemoryUniquePtr<T>(memoryNewFrom<T>(allocator, std::forward<Args>(args)...));
}

/**
 * @brief 永続ヒープ上にオブジェクトを生成し、unique_ptrで所有する
 * @param args コンストラクタ引数
 * @return MemoryUniquePtr<T> 生成したオブジェクト
 */
template <class T, class... Args>
MemoryUniquePtr<T> memoryMakeUnique(Args&&... args)
{
    return MemoryUniquePtr<T>(memoryNew<T>(std::forward<Args>(args)...));
}

/**
 * @brief プールからオブジェクトを生成する
 * @param pool 確保元のプール
 * @param args コンストラクタ引数
 * @return T* 生成したオブジェクト。失敗した場合はnullptr
 */
template <class T, class... Args>
T* poolNew(PoolAllocator& pool, Args&&... args)
{
    void* memory = pool.allocate(sizeof(T), memoryAlignmentOf<T>());
    if (memory == nullptr)
        return nullptr;

    return new (memory) T(std::forward<Args>(args)...);
}

/**
 * @brief プールから生成したオブジェクトを破棄する
 * @param pool 確保元のプール
 * @param pointer 破棄するオブジェクト
 */
template <class T>
void poolDelete(PoolAllocator& pool, T* pointer)
{
    if (pointer == nullptr)
        return;

    pointer->~T();
    pool.deallocate(pointer);
}

} // namespace Engine

// 利用者向けマクロ
// 可変引数が空でも壊れないよう、固定引数もすべて__VA_ARGS__側へ渡している

//! 永続ヒープからメモリを確保する
#define GE_ALLOC(size)                          (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::UNKNOWN), ::Engine::memoryAllocate(size))

//! 用途タグを指定して永続ヒープからメモリを確保する
#define GE_ALLOC_TAG(size, tag)                 (::Engine::memorySetSource(__FILE__, __LINE__, tag), ::Engine::memoryAllocate(size))

//! アライメントと用途タグを指定して永続ヒープからメモリを確保する
#define GE_ALLOC_ALIGNED(size, alignment, tag)  (::Engine::memorySetSource(__FILE__, __LINE__, tag), ::Engine::memoryAllocate(size, alignment))

//! 指定したアロケータからメモリを確保する
#define GE_ALLOC_FROM(allocator, size, tag)     (::Engine::memorySetSource(__FILE__, __LINE__, tag), ::Engine::memoryAllocateFrom(allocator, size))

//! フレームヒープからメモリを確保する
#define GE_FRAME_ALLOC(size)                    (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::FRAME), ::Engine::memoryAllocateFrame(size))

//! メモリを解放する
#define GE_FREE(pointer)                        ::Engine::memoryFree(pointer)

//! 永続ヒープ上にオブジェクトを生成する GE_NEW(型, コンストラクタ引数...)
#define GE_NEW(type, ...)                       (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::UNKNOWN), ::Engine::memoryNew<type>(__VA_ARGS__))

//! 用途タグを指定して永続ヒープ上にオブジェクトを生成する GE_NEW_TAG(型, タグ, コンストラクタ引数...)
#define GE_NEW_TAG(type, tag, ...)              (::Engine::memorySetSource(__FILE__, __LINE__, tag), ::Engine::memoryNew<type>(__VA_ARGS__))

//! 指定したアロケータ上にオブジェクトを生成する GE_NEW_FROM(型, アロケータ, コンストラクタ引数...)
#define GE_NEW_FROM(type, ...)                  (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::UNKNOWN), ::Engine::memoryNewFrom<type>(__VA_ARGS__))

//! フレームヒープ上にオブジェクトを生成する GE_FRAME_NEW(型, コンストラクタ引数...)
#define GE_FRAME_NEW(type, ...)                 (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::FRAME), ::Engine::memoryNewFrame<type>(__VA_ARGS__))

//! オブジェクトを破棄する
#define GE_DELETE(pointer)                      ::Engine::memoryDelete(pointer)

//! 永続ヒープ上に配列を生成する
#define GE_NEW_ARRAY(type, count)               (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::UNKNOWN), ::Engine::memoryNewArray<type>(count))

//! 配列を破棄する
#define GE_DELETE_ARRAY(pointer)                ::Engine::memoryDeleteArray(pointer)

//! 永続ヒープ上にオブジェクトを生成し、unique_ptrで所有する GE_MAKE_UNIQUE(型, コンストラクタ引数...)
#define GE_MAKE_UNIQUE(type, ...)               (::Engine::memorySetSource(__FILE__, __LINE__, ::Engine::MemoryTag::UNKNOWN), ::Engine::memoryMakeUnique<type>(__VA_ARGS__))
