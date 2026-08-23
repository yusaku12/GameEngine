#pragma once

#include "Core\CoreDefines.h"
#include "Core\Logging\Assert.h"

namespace Engine
{
    static constexpr size_t MEMORY_KIB = static_cast<size_t>(1024);          //!< 1KiB
    static constexpr size_t MEMORY_MIB = MEMORY_KIB * static_cast<size_t>(1024); //!< 1MiB
    static constexpr size_t MEMORY_GIB = MEMORY_MIB * static_cast<size_t>(1024); //!< 1GiB

    //! 既定のアライメント（DirectXMathのXMVECTORが16バイト境界を要求するため）
    static constexpr size_t MEMORY_DEFAULT_ALIGNMENT = static_cast<size_t>(16);

    //! ヒープを切り出す際の境界
    static constexpr size_t MEMORY_PAGE_ALIGNMENT = static_cast<size_t>(4096);

    //! アロケータ名の最大長（終端文字を含む）
    static constexpr size_t MEMORY_NAME_LENGTH = static_cast<size_t>(32);

    //! ユーザ領域の後方に置く番兵のサイズ
    static constexpr size_t MEMORY_GUARD_SIZE = static_cast<size_t>(8);

    static constexpr uint8_t MEMORY_GUARD_PATTERN = static_cast<uint8_t>(0xFD); //!< 番兵の値
    static constexpr uint8_t MEMORY_ALLOCATED_PATTERN = static_cast<uint8_t>(0xCD); //!< 確保直後の値
    static constexpr uint8_t MEMORY_FREED_PATTERN = static_cast<uint8_t>(0xDD); //!< 解放直後の値

    /**
     * @brief メモリの用途を表す列挙型
     * 統計表示とリーク解析のカテゴリ分けに使用する
     */
    enum class MemoryTag : uint8_t
    {
        UNKNOWN,    //!< 未分類
        SYSTEM,     //!< エンジン基盤
        GRAPHICS,   //!< 描画関連
        RESOURCE,   //!< リソース（テクスチャ・モデル）
        AUDIO,      //!< サウンド関連
        SCENE,      //!< シーン・エンティティ
        FRAME,      //!< フレーム内一時
        CONTAINER,  //!< STLコンテナ
        DEBUG_INFO, //!< デバッグ機能自身
        COUNT,      //!< タグ総数
    };

    /**
     * @brief メモリの使用状況
     */
    struct MemoryStats
    {
        size_t capacity = 0;        //!< 管理している総容量（バイト）
        size_t used = 0;            //!< 使用中のサイズ（バイト）
        size_t peak = 0;            //!< 使用サイズのピーク（バイト）
        size_t allocationCount = 0; //!< 現在有効な割り当て数
        size_t allocationTotal = 0; //!< 累計の割り当て回数
    };

    /**
     * @brief メモリタグの表示名を取得する
     * @param tag メモリタグ
     * @return const char* 表示名
     */
    const char* memoryTagName(MemoryTag tag);
} // namespace Engine

#if GE_MEMORY_TRACKING
#define GE_MEMORY_ASSERT(expression, message) GE_ASSERT_MSG(expression, message)
#else
#define GE_MEMORY_ASSERT(expression, message) ((void)sizeof(static_cast<bool>(expression)))
#endif
