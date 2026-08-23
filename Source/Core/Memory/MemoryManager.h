#pragma once

#include "Core\Memory\FreeListAllocator.h"
#include "Core\Memory\LinearAllocator.h"
#include "Core\Memory\PoolAllocator.h"

namespace Engine
{
    /**
     * @brief メモリシステムの構成設定
     */
    struct MemoryConfig
    {
        size_t persistentSize = static_cast<size_t>(128) * MEMORY_MIB; //!< 永続ヒープのサイズ
        size_t frameSize = static_cast<size_t>(16) * MEMORY_MIB;       //!< フレームヒープ1面あたりのサイズ
        size_t trackerCapacity = static_cast<size_t>(65536);           //!< 同時に追跡する割り当て数の目安
        bool   enableTracking = true;                                  //!< 追跡機能を使用するか
        bool   enableFillPattern = true;                               //!< 確保・解放時にフィルパターンを書き込むか
    };

    /**
     * @brief メモリ管理クラス
     * 起動時にOSからメモリを一括で借り受け、用途別のアロケータへ分配する
     * エンジンの動的確保はすべてこのクラスが管理する領域から行う
     */
    class MemoryManager
    {
    public:

        /**
         * @brief インスタンスを取得する
         * @return MemoryManager& インスタンス
         */
        static MemoryManager& instance()
        {
            static MemoryManager instance;
            return instance;
        }

        /**
         * @brief メモリシステムを初期化する
         * @param config 構成設定
         * @return bool 成功したらtrue
         */
        bool initialize(const MemoryConfig& config = MemoryConfig{});

        /**
         * @brief メモリシステムを終了する
         * リーク検出の結果を出力してからルートメモリを解放する
         * @return size_t 検出したリーク件数
         */
        size_t finalize();

        /**
         * @brief フレームの開始処理
         * フレームヒープを切り替え、これから使う面を巻き戻す
         */
        void beginFrame();

        /**
         * @brief 初期化済みかを取得する
         * @return bool 初期化済みならtrue
         */
        bool isInitialized() const { return m_initialized; }

        /**
         * @brief 永続アロケータを取得する
         * @return IAllocator& 永続アロケータ
         */
        IAllocator& getPersistentAllocator() { return m_persistent; }

        /**
         * @brief フレームアロケータを取得する
         * @return IAllocator& 現在のフレームアロケータ
         */
        IAllocator& getFrameAllocator() { return m_frame[m_frameIndex]; }

        /**
         * @brief 永続ヒープを取得する（断片化の確認などに使用する）
         * @return const FreeListAllocator& 永続ヒープ
         */
        const FreeListAllocator& getPersistentHeap() const { return m_persistent; }

        /**
         * @brief 永続ヒープからメモリを切り出してプールを構築する
         * @param pool 構築するプール
         * @param blockSize 1ブロックのサイズ（バイト）
         * @param blockCount ブロック数
         * @param alignment ブロックのアライメント
         * @param name プール名
         * @return bool 成功したらtrue
         */
        bool createPool(PoolAllocator& pool, size_t blockSize, size_t blockCount, size_t alignment, const char* name);

        /**
         * @brief createPool()で構築したプールを破棄する
         * @param pool 破棄するプール
         */
        void destroyPool(PoolAllocator& pool);

        /**
         * @brief 全体の使用状況を取得する
         * @return MemoryStats 使用状況
         */
        MemoryStats getStats() const;

        /**
         * @brief 使用状況をログへ出力する
         */
        void dumpStats() const;

        /**
         * @brief 経過フレーム数を取得する
         * @return uint64_t フレーム数
         */
        uint64_t getFrameCount() const { return m_frameCount; }

    private:

        MemoryManager() = default;
        ~MemoryManager() = default;

        MemoryManager(const MemoryManager&) = delete;
        MemoryManager(MemoryManager&&) = delete;
        MemoryManager& operator=(const MemoryManager&) = delete;
        MemoryManager& operator=(MemoryManager&&) = delete;

        void* m_rootMemory = nullptr; //!< OSから確保したルートメモリ
        size_t            m_rootSize = 0;         //!< ルートメモリのサイズ
        FreeListAllocator m_persistent;           //!< 永続ヒープ
        LinearAllocator   m_frame[2];             //!< フレームヒープ（ダブルバッファ）
        size_t            m_frameIndex = 0;       //!< 現在のフレームヒープ番号
        uint64_t          m_frameCount = 0;       //!< 経過フレーム数
        bool              m_initialized = false;  //!< 初期化済みか
    };
} // namespace Engine
