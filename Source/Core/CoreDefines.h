#pragma once

// Windowsヘッダが定義するマクロは標準ライブラリと衝突するため取り除く
#ifdef ERROR
#undef ERROR
#endif
#ifdef DEBUG
#undef DEBUG
#endif

//! デバッグビルドかどうか
#if defined(_DEBUG)
#define GE_DEBUG_BUILD 1
#else
#define GE_DEBUG_BUILD 0
#endif

//! アサートを有効にするか
#define GE_ASSERT_ENABLED GE_DEBUG_BUILD

//! メモリ追跡機能（リーク検出・破壊検出・フィルパターン）を有効にするか
#define GE_MEMORY_TRACKING GE_DEBUG_BUILD

//! 強制インライン化
#define GE_FORCEINLINE __forceinline

//! インライン化の禁止
#define GE_NOINLINE __declspec(noinline)

//! デバッガでの中断
#define GE_DEBUG_BREAK() __debugbreak()

//! 引数の未使用を明示する
#define GE_UNUSED(value) ((void)(value))

//! 固定長配列の要素数
#define GE_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

//! キャッシュラインのサイズ
#define GE_CACHE_LINE_SIZE 64

//! コピーを禁止する
#define GE_DISABLE_COPY(type)          \
    type(const type&) = delete;        \
    type& operator=(const type&) = delete

//! ムーブを禁止する
#define GE_DISABLE_MOVE(type)     \
    type(type&&) = delete;        \
    type& operator=(type&&) = delete

//! コピーとムーブを禁止する
#define GE_DISABLE_COPY_AND_MOVE(type) \
    GE_DISABLE_COPY(type);             \
    GE_DISABLE_MOVE(type)
