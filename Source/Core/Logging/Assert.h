#pragma once

#include <string>

#include "Core\CoreDefines.h"
#include "Core\Logging\Logger.h"

namespace Engine
{

/**
 * @brief アサート失敗時の処理
 * ログへ出力し、デバッガが接続されていれば中断する
 * @param expression 失敗した条件式
 * @param message 補足メッセージ
 * @param loc 呼び出し元のソース位置
 * @return bool 常にfalse（マクロの短絡評価で使用する）
 */
bool assertFailed(const char* expression, std::string_view message, spdlog::source_loc loc);

/**
 * @brief フォーマット付きのアサート失敗処理
 * @param expression 失敗した条件式
 * @param loc 呼び出し元のソース位置
 * @param format フォーマット文字列
 * @param args フォーマット引数
 * @return bool 常にfalse（マクロの短絡評価で使用する）
 */
template <class... Args>
bool assertFailedFormat(const char* expression, spdlog::source_loc loc, spdlog::format_string_t<Args...> format, Args&&... args)
{
    return assertFailed(expression, spdlog::fmt_lib::format(format, std::forward<Args>(args)...), loc);
}

/**
 * @brief 復帰不能なエラーとしてプロセスを終了する
 * @param message エラー内容
 * @param loc 呼び出し元のソース位置
 */
[[noreturn]] void fatalError(std::string_view message, spdlog::source_loc loc);

} // namespace Engine

#if GE_ASSERT_ENABLED

//! 条件を満たさない場合にログ出力して中断する
#define GE_ASSERT(expression) \
    (void)((!!(expression)) || ::Engine::assertFailed(#expression, "", GE_LOG_SOURCE))

//! メッセージ付きのアサート GE_ASSERT_MSG(条件, フォーマット, 引数...)
#define GE_ASSERT_MSG(expression, ...) \
    (void)((!!(expression)) || ::Engine::assertFailedFormat(#expression, GE_LOG_SOURCE, __VA_ARGS__))

//! リリースビルドでも条件式を評価するアサート
#define GE_VERIFY(expression) GE_ASSERT(expression)

#else

#define GE_ASSERT(expression)             ((void)sizeof(static_cast<bool>(expression)))
#define GE_ASSERT_MSG(expression, ...)    ((void)sizeof(static_cast<bool>(expression)))
#define GE_VERIFY(expression)             ((void)(expression))

#endif

//! 到達してはならない箇所を示す
#define GE_UNREACHABLE() ::Engine::fatalError("到達してはならないコードに到達しました", GE_LOG_SOURCE)

//! 未実装であることを示す
#define GE_NOT_IMPLEMENTED() ::Engine::fatalError("未実装の処理が呼び出されました", GE_LOG_SOURCE)

//! 復帰不能なエラーとして終了する GE_FATAL(フォーマット, 引数...)
#define GE_FATAL(...) ::Engine::fatalError(spdlog::fmt_lib::format(__VA_ARGS__), GE_LOG_SOURCE)
