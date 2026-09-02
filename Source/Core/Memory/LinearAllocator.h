#pragma once

#include "Core\Memory\IAllocator.h"

namespace Engine
{
    /**
     * @brief 線形アロケータ
     * ポインタを進めるだけでメモリを切り出す。個別解放は行わずreset()で一括で巻き戻す
     * フレーム内の一時メモリや起動時のスクラッチ領域に使用する
     */
    class LinearAllocator final : public IAllocator
    {
    public:

        LinearAllocator() = default;
        ~LinearAllocator() override;

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

        /**
         * @brief 何もしない（線形アロケータは個別解放に対応しない）
         */
        void deallocate(void* pointer) override;

        bool supportsDeallocate() const override { return false; }
        bool owns(const void* pointer) const override;

        /**
         * @brief 確保位置を先頭まで巻き戻す
         */
        void reset();

        /**
         * @brief 残り容量を取得する
         * @return size_t 残り容量（バイト）
         */
        size_t getRemaining() const { return m_capacity - m_offset; }

        /**
         * @brief 使用状況を取得する
         * @return const MemoryStats& 使用状況
         */
        const MemoryStats& getStats() const override { return m_stats; }
        const char* getName() const override { return m_name; }

    private:

        unsigned char* m_start = nullptr;      //!< 管理対象の先頭
        size_t         m_capacity = 0;         //!< 管理対象のサイズ
        size_t         m_offset = 0;           //!< 次に切り出す位置
        MemoryStats    m_stats{};              //!< 使用状況
        char           m_name[MEMORY_NAME_LENGTH]{}; //!< アロケータ名
    };
} // namespace Engine
