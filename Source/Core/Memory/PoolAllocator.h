#pragma once

#include "Core\Memory\IAllocator.h"

namespace Engine
{

/**
 * @brief 固定サイズのプールアロケータ
 * 未使用ブロックを侵入型のフリーリストで連結するため、確保も解放もO(1)で行える
 * 同じサイズのオブジェクトを大量に生成する用途に使用する
 */
class PoolAllocator final : public IAllocator
{
public:

    PoolAllocator() = default;
    ~PoolAllocator() override;

    /**
     * @brief 初期化する
     * @param memory 管理対象のメモリ先頭
     * @param size 管理対象のサイズ（バイト）
     * @param blockSize 1ブロックのサイズ（バイト）
     * @param alignment ブロックのアライメント（2の累乗であること）
     * @param name アロケータ名
     * @return bool 成功したらtrue
     */
    bool initialize(void* memory, size_t size, size_t blockSize, size_t alignment, const char* name);

    /**
     * @brief 終了処理を行う
     */
    void finalize();

    void* allocate(size_t size, size_t alignment = MEMORY_DEFAULT_ALIGNMENT) override;
    void deallocate(void* pointer) override;

    bool supportsDeallocate() const override { return true; }
    bool owns(const void* pointer) const override;

    /**
     * @brief 1ブロックのサイズを取得する
     * @return size_t ブロックサイズ（バイト）
     */
    size_t getBlockSize() const { return m_blockSize; }

    /**
     * @brief 総ブロック数を取得する
     * @return size_t ブロック数
     */
    size_t getBlockCount() const { return m_blockCount; }

    /**
     * @brief 初期化時に受け取ったメモリ先頭を取得する
     * @return void* メモリ先頭
     */
    void* getBaseMemory() const { return m_base; }

    const MemoryStats& getStats() const override { return m_stats; }
    const char* getName() const override { return m_name; }

private:

    void*          m_base = nullptr;       //!< 初期化時に受け取ったメモリ先頭
    unsigned char* m_start = nullptr;      //!< アライメント調整後の先頭
    size_t         m_blockSize = 0;        //!< 1ブロックのサイズ
    size_t         m_blockCount = 0;       //!< 総ブロック数
    size_t         m_alignment = 0;        //!< ブロックのアライメント
    void**         m_freeList = nullptr;   //!< 未使用ブロックの先頭
    MemoryStats    m_stats{};              //!< 使用状況
    char           m_name[MEMORY_NAME_LENGTH]{}; //!< アロケータ名
};

} // namespace Engine
