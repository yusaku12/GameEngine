#pragma once

#include "Core\Memory\IAllocator.h"

namespace Engine
{
    /**
     * @brief 可変長に対応した汎用アロケータ
     * 空きブロックをアドレス昇順のリストで管理し、最良適合（Best Fit）で切り出す
     * 解放時は前後の空きブロックと結合して断片化を抑える
     */
    class FreeListAllocator final : public IAllocator
    {
    public:

        FreeListAllocator() = default;
        ~FreeListAllocator() override;

        /**
         * @brief 初期化する
         * @param memory 管理対象のメモリ先頭
         * @param size 管理対象のサイズ（バイト）
         * @param name アロケータ名
         * @return bool 成功したらtrue
         */
        bool initialize(void* memory, size_t size, const char* name);

        /**
         * @brief 終了処理を行う
         */
        void finalize();

        void* allocate(size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT) override;
        void deallocate(void* pointer) override;

        bool supportsDeallocate() const override { return true; }
        bool owns(const void* pointer) const override;

        /**
         * @brief 最大の連続空き領域を取得する
         * @return size_t サイズ（バイト）
         */
        size_t getLargestFreeBlock() const;

        /**
         * @brief 空きブロックの個数を取得する
         * @return size_t 空きブロック数
         */
        size_t getFreeBlockCount() const;

        const MemoryStats& getStats() const override { return m_stats; }
        const char* getName() const override { return m_name; }

    private:

        /**
         * @brief 空きブロック（空き領域そのものに配置する）
         */
        struct FreeBlock
        {
            size_t     size; //!< このブロックのサイズ
            FreeBlock* next; //!< 次の空きブロック（アドレス昇順）
        };

        /**
         * @brief 確保済みブロックの管理情報（ユーザ領域の直前に配置する）
         */
        struct BlockHeader
        {
            size_t   size;       //!< ブロック全体のサイズ
            uint32_t adjustment; //!< ブロック先頭からユーザ領域までの調整量
            uint32_t reserved;   //!< 予約領域（FreeBlockと同じサイズにするための詰め物）
        };

        /**
         * @brief 空きブロックをリストへ挿入し、前後と結合する
         * @param block 挿入するブロック
         */
        void insertFreeBlock(FreeBlock* block);

        unsigned char* m_start = nullptr;      //!< 管理対象の先頭
        size_t         m_capacity = 0;         //!< 管理対象のサイズ
        FreeBlock* m_freeList = nullptr;   //!< 空きブロックの先頭
        MemoryStats    m_stats{};              //!< 使用状況
        char           m_name[MEMORY_NAME_LENGTH]{}; //!< アロケータ名
    };
} // namespace Engine
