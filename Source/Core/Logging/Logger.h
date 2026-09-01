#pragma once

#include <spdlog/fmt/fmt.h>
#include <spdlog/logger.h>
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief ログレベルを表す列挙型
     */
    enum class LogLevel : unsigned int
    {
        TRACE,    //!< 詳細な追跡情報
        DEBUG,    //!< デバッグ情報
        INFO,     //!< 通常の情報
        WARNING,  //!< 警告
        ERROR,    //!< エラー
        CRITICAL, //!< 致命的なエラー
        OFF,      //!< 出力しない
    };

    /**
     * @brief ロガーの構成設定
     */
    struct LoggerConfig
    {
        std::string fileName = "Log/Engine.log"; //!< ログファイルの出力先
        LogLevel    level = LogLevel::TRACE;     //!< 出力する最小のログレベル
        LogLevel    flushLevel = LogLevel::WARNING; //!< 即時フラッシュする最小のログレベル
        bool        useConsole = true;           //!< コンソールへ出力するか
        bool        useFile = true;              //!< ファイルへ出力するか
        bool        useDebugOutput = true;       //!< デバッグ出力へ出力するか
        bool        truncate = true;             //!< 起動時にログファイルを空にするか
    };

    /**
     * @brief ログ出力を行うクラス
     * spdlogを内部で使用し、コンソール・ファイル・デバッグ出力へ同時に書き出す
     */
    class Logger
    {
    public:

        /**
         * @brief インスタンスを取得する
         * @return Logger& インスタンス
         */
        static Logger& instance()
        {
            static Logger instance;
            return instance;
        }

        /**
         * @brief ロガーを初期化する
         * @param config 構成設定
         * @return bool 成功したらtrue
         */
        bool initialize(const LoggerConfig& config = LoggerConfig{});

        /**
         * @brief ロガーの終了処理を行う
         */
        void finalize();

        /**
         * @brief 初期化済みかを取得する
         * @return bool 初期化済みならtrue
         */
        bool isInitialized() const { return m_logger != nullptr; }

        /**
         * @brief 出力する最小のログレベルを設定する
         * @param level ログレベル
         */
        void setLevel(LogLevel level);

        /**
         * @brief 出力する最小のログレベルを取得する
         * @return LogLevel ログレベル
         */
        LogLevel getLevel() const { return m_level; }

        /**
         * @brief 指定したレベルが出力対象かを判定する
         * @param level ログレベル
         * @return bool 出力対象ならtrue
         */
        bool isEnabled(LogLevel level) const { return m_logger != nullptr && level >= m_level && level != LogLevel::OFF; }

        /**
         * @brief 未書き出しのログを書き出す
         */
        void flush();

        /**
         * @brief ログを出力する
         * @param level ログレベル
         * @param loc 呼び出し元のソース位置
         * @param format フォーマット文字列
         * @param args フォーマット引数
         */
        template <class... Args>
        void log(LogLevel level, spdlog::source_loc loc, spdlog::format_string_t<Args...> format, Args&&... args)
        {
            if (!isEnabled(level))
                return;

            m_logger->log(loc, toSpdlogLevel(level), format, std::forward<Args>(args)...);
        }

        /**
         * @brief カテゴリを付けてログを出力する
         * @param level ログレベル
         * @param category カテゴリ名
         * @param loc 呼び出し元のソース位置
         * @param format フォーマット文字列
         * @param args フォーマット引数
         */
        template <class... Args>
        void logCategory(LogLevel level, const char* category, spdlog::source_loc loc, spdlog::format_string_t<Args...> format, Args&&... args)
        {
            if (!isEnabled(level))
                return;

            m_logger->log(loc, toSpdlogLevel(level), "[{}] {}", category, spdlog::fmt_lib::format(format, std::forward<Args>(args)...));
        }

        /**
         * @brief 整形済みの文字列をそのまま出力する
         * @param level ログレベル
         * @param loc 呼び出し元のソース位置
         * @param message 出力する文字列
         */
        void logMessage(LogLevel level, spdlog::source_loc loc, std::string_view message);

    private:

        Logger() = default;
        ~Logger() = default;

        GE_DISABLE_COPY_AND_MOVE(Logger);

        /**
         * @brief LogLevelをspdlogのログレベルへ変換する
         * @param level ログレベル
         * @return spdlog::level::level_enum spdlogのログレベル
         */
        static spdlog::level::level_enum toSpdlogLevel(LogLevel level);

        std::shared_ptr<spdlog::logger> m_logger;             //!< ログ出力先
        LogLevel                        m_level = LogLevel::TRACE; //!< 出力する最小のログレベル
    };
} // namespace Engine

//! 呼び出し元のソース位置
#define GE_LOG_SOURCE spdlog::source_loc{ __FILE__, __LINE__, SPDLOG_FUNCTION }

#define LOG_TRACE(...)    ::Engine::Logger::instance().log(::Engine::LogLevel::TRACE,    GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_DEBUG(...)    ::Engine::Logger::instance().log(::Engine::LogLevel::DEBUG,    GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_INFO(...)     ::Engine::Logger::instance().log(::Engine::LogLevel::INFO,     GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_WARNING(...)  ::Engine::Logger::instance().log(::Engine::LogLevel::WARNING,  GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_ERROR(...)    ::Engine::Logger::instance().log(::Engine::LogLevel::ERROR,    GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_CRITICAL(...) ::Engine::Logger::instance().log(::Engine::LogLevel::CRITICAL, GE_LOG_SOURCE, __VA_ARGS__)

#define LOG_TRACE_CAT(category, ...)    ::Engine::Logger::instance().logCategory(::Engine::LogLevel::TRACE,    category, GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_DEBUG_CAT(category, ...)    ::Engine::Logger::instance().logCategory(::Engine::LogLevel::DEBUG,    category, GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_INFO_CAT(category, ...)     ::Engine::Logger::instance().logCategory(::Engine::LogLevel::INFO,     category, GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_WARNING_CAT(category, ...)  ::Engine::Logger::instance().logCategory(::Engine::LogLevel::WARNING,  category, GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_ERROR_CAT(category, ...)    ::Engine::Logger::instance().logCategory(::Engine::LogLevel::ERROR,    category, GE_LOG_SOURCE, __VA_ARGS__)
#define LOG_CRITICAL_CAT(category, ...) ::Engine::Logger::instance().logCategory(::Engine::LogLevel::CRITICAL, category, GE_LOG_SOURCE, __VA_ARGS__)
