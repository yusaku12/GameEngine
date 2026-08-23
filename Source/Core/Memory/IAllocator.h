#pragma once

#include "Core\Memory\MemoryTypes.h"

namespace Engine
{
    /**
     * @brief アロケータの共通インターフェース
     * 実装クラスはinitialize()で管理対象のメモリを受け取る
     */
    class IAllocator
    {
    public:

        IAllocator() = default;
        virtual ~IAllocator() = default;

        IAllocator(const IAllocator&) = delete;
        IAllocator(IAllocator&&) = delete;
        IAllocator& operator=(const IAllocator&) = delete;
        IAllocator& operator=(IAllocator&&) = delete;

        /**
         * @brief メモリを確保する
         * @param size 確保するサイズ（バイト）
         * @param alignment アライメント（2の累乗であること）
         * @return void* 確保したメモリ。失敗した場合はnullptr
         */
        virtual void* allocate(size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT) = 0;

        /**
         * @brief メモリを解放する
         * @param pointer allocate()が返したポインタ
         */
        virtual void deallocate(void* pointer) = 0;

        /**
         * @brief 個別解放に対応しているかを取得する
         * @return bool 対応していればtrue
         */
        virtual bool supportsDeallocate() const = 0;

        /**
         * @brief 指定ポインタが自身の管理範囲かを判定する
         * @param pointer 判定するポインタ
         * @return bool 管理範囲内ならtrue
         */
        virtual bool owns(const void* pointer) const = 0;

        /**
         * @brief 使用状況を取得する
         * @return const MemoryStats& 使用状況
         */
        virtual const MemoryStats& getStats() const = 0;

        /**
         * @brief アロケータ名を取得する
         * @return const char* アロケータ名
         */
        virtual const char* getName() const = 0;
    };
} // namespace Engine
