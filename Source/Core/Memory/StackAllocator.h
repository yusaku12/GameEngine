#pragma once

#include "Core\Memory\IAllocator.h"

namespace Engine
{

//! スタックアロケータの巻き戻し位置
using MemoryMarker = size_t;

/**
 * @brief スタックアロケータ
 * 確保と逆順（LIFO）での解放と、マーカーによる一括巻き戻しに対応する
 * 階層的な読み込み処理など、スコープ単位で寿命が決まるメモリに使用する
 */
class StackAllocator final : public IAllocator
{
public:

    StackAllocator() = default;
    ~StackAllocator() override;

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
     * @brief 直前に確保したメモリを解放する
     * @param pointer allocate()が返した最後のポインタ
     */
    void deallocate(void* pointer) override;

    bool supportsDeallocate() const override { return true; }
    bool owns(const void* pointer) const override;

    /**
     * @brief 現在の確保位置を取得する
     * @return MemoryMarker 巻き戻し用のマーカー
     */
    MemoryMarker getMarker() const { return m_offset; }

    /**
     * @brief 指定したマーカーまで巻き戻す
     * @param marker getMarker()が返したマーカー
     */
    void freeToMarker(MemoryMarker marker);

    /**
     * @brief 確保位置を先頭まで巻き戻す
     */
    void reset();

    /**
     * @brief 残り容量を取得する
     * @return size_t 残り容量（バイト）
     */
    size_t getRemaining() const { return m_capacity - m_offset; }

    const MemoryStats& getStats() const override { return m_stats; }
    const char* getName() const override { return m_name; }

private:

    /**
     * @brief 巻き戻しに必要な情報
     */
    struct StackHeader
    {
        uint32_t adjustment; //!< ブロック先頭からユーザ領域までの調整量
    };

    unsigned char* m_start = nullptr;      //!< 管理対象の先頭
    size_t         m_capacity = 0;         //!< 管理対象のサイズ
    size_t         m_offset = 0;           //!< 次に切り出す位置
    MemoryStats    m_stats{};              //!< 使用状況
    char           m_name[MEMORY_NAME_LENGTH]{}; //!< アロケータ名
};

/**
 * @brief スコープを抜けるときにスタックアロケータを自動で巻き戻すクラス
 */
class ScopedMemoryMarker
{
public:

    /**
     * @brief コンストラクタ
     * @param allocator 対象のスタックアロケータ
     */
    explicit ScopedMemoryMarker(StackAllocator& allocator)
        : m_allocator(allocator)
        , m_marker(allocator.getMarker())
    {
    }

    ~ScopedMemoryMarker()
    {
        m_allocator.freeToMarker(m_marker);
    }

    ScopedMemoryMarker(const ScopedMemoryMarker&) = delete;
    ScopedMemoryMarker(ScopedMemoryMarker&&) = delete;
    ScopedMemoryMarker& operator=(const ScopedMemoryMarker&) = delete;
    ScopedMemoryMarker& operator=(ScopedMemoryMarker&&) = delete;

private:

    StackAllocator& m_allocator; //!< 対象のアロケータ
    MemoryMarker    m_marker;    //!< 巻き戻し位置
};

} // namespace Engine
