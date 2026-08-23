#pragma once

#include "Core\Memory\MemoryTypes.h"

namespace Engine
{

/**
 * @brief メモリ割り当ての追跡クラス
 * GE_MEMORY_TRACKINGが有効なビルドでのみ動作し、リーク検出と用途別統計を提供する
 * 記録表そのものは追跡対象外のメモリ（std::malloc）から確保する
 */
class MemoryTracker
{
public:

    /**
     * @brief インスタンスを取得する
     * @return MemoryTracker& インスタンス
     */
    static MemoryTracker& instance()
    {
        static MemoryTracker instance;
        return instance;
    }

    /**
     * @brief 追跡を開始する
     * @param capacity 同時に追跡できる割り当て数の目安
     * @return bool 成功したらtrue
     */
    bool initialize(size_t capacity);

    /**
     * @brief 追跡を終了する
     */
    void finalize();

    /**
     * @brief 追跡が有効かを取得する
     * @return bool 有効ならtrue
     */
    bool isEnabled() const { return m_entries != nullptr; }

    /**
     * @brief 割り当てを記録する
     * @param pointer 割り当てたメモリ
     * @param size 要求サイズ（バイト）
     * @param tag 用途タグ
     * @param allocatorName 割り当て元アロケータ名
     * @param file 呼び出し元ファイル
     * @param line 呼び出し元行
     */
    void recordAllocate(const void* pointer, size_t size, MemoryTag tag, const char* allocatorName, const char* file, int line);

    /**
     * @brief 解放を記録する
     * @param pointer 解放したメモリ
     */
    void recordDeallocate(const void* pointer);

    /**
     * @brief 指定範囲の記録をまとめて破棄する
     * 線形アロケータやスタックアロケータの巻き戻し時に使用する
     * @param begin 範囲の先頭
     * @param size 範囲のサイズ（バイト）
     */
    void forgetRange(const void* begin, size_t size);

    /**
     * @brief 未解放のメモリをログへ出力する
     * @return size_t リーク件数
     */
    size_t dumpLeaks() const;

    /**
     * @brief 用途別の使用状況をログへ出力する
     */
    void dumpStats() const;

    /**
     * @brief 用途別の使用状況を取得する
     * @param tag 用途タグ
     * @return const MemoryStats& 使用状況
     */
    const MemoryStats& getTagStats(MemoryTag tag) const;

    /**
     * @brief 追跡中の割り当て数を取得する
     * @return size_t 割り当て数
     */
    size_t getLiveCount() const { return m_liveCount; }

    /**
     * @brief 追跡中の合計サイズを取得する
     * @return size_t サイズ（バイト）
     */
    size_t getLiveSize() const { return m_liveSize; }

private:

    /**
     * @brief 記録表のスロット状態
     */
    enum class SlotState : uint8_t
    {
        EMPTY,    //!< 未使用
        OCCUPIED, //!< 使用中
        DELETED,  //!< 削除済み（探索を継続するための墓標）
    };

    /**
     * @brief 割り当ての記録
     */
    struct Entry
    {
        const void* pointer;       //!< 割り当てたメモリ
        size_t      size;          //!< 要求サイズ
        const char* file;          //!< 呼び出し元ファイル
        const char* allocatorName; //!< 割り当て元アロケータ名
        uint32_t    line;          //!< 呼び出し元行
        uint32_t    serial;        //!< 割り当て通し番号
        MemoryTag   tag;           //!< 用途タグ
        SlotState   state;         //!< スロット状態
    };

    MemoryTracker() = default;
    ~MemoryTracker() = default;

    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker(MemoryTracker&&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;
    MemoryTracker& operator=(MemoryTracker&&) = delete;

    /**
     * @brief 記録済みのスロットを探す
     * @param pointer 検索するポインタ
     * @return Entry* 見つかったスロット。無ければnullptr
     */
    Entry* findEntry(const void* pointer) const;

    /**
     * @brief 記録を取り除く
     * @param entry 対象のスロット
     */
    void removeEntry(Entry* entry);

    Entry*      m_entries = nullptr;     //!< 記録表
    size_t      m_capacity = 0;          //!< 記録表の容量（2の累乗）
    size_t      m_used = 0;              //!< 使用中スロット数（墓標を含む）
    size_t      m_liveCount = 0;         //!< 追跡中の割り当て数
    size_t      m_liveSize = 0;          //!< 追跡中の合計サイズ
    uint32_t    m_serial = 0;            //!< 割り当て通し番号
    bool        m_overflowReported = false; //!< 容量不足を報告済みか
    MemoryStats m_tagStats[static_cast<size_t>(MemoryTag::COUNT)]{}; //!< 用途別の使用状況
};

} // namespace Engine
