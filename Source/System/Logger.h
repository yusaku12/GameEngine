#pragma once

#include <spdlog/logger.h>

/*
* @brief ログレベルを表す列挙型
*/
enum class LogLevel : unsigned int
{
    INFO,
    WARNING,
    ERROR,
};

/*
* @brief ログ出力を行うクラス
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
     * @brief ロガーの初期化
     * @param fileName ログファイルの出力先
     */
    void initialize(const std::string& fileName = "Log/Engine.log");

    /**
     * @brief ロガーの終了処理
     */
    void finalize();

    /**
     * @brief ログを出力する
     * @param level ログレベル
     * @param loc 呼び出し元のソース位置
     * @param fmt フォーマット文字列
     * @param args フォーマット引数
     */
    template <class... Args>
    void log(LogLevel level, spdlog::source_loc loc, spdlog::format_string_t<Args...> fmt, Args&&... args)
    {
        if (!m_logger)
            return;

        m_logger->log(loc, toSpdlogLevel(level), fmt, std::forward<Args>(args)...);
    }

private:

    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

    /**
     * @brief LogLevelをspdlogのログレベルへ変換する
     * @param level ログレベル
     * @return spdlog::level::level_enum spdlogのログレベル
     */
    static spdlog::level::level_enum toSpdlogLevel(LogLevel level);

    std::shared_ptr<spdlog::logger> m_logger; //!< ログ出力先
};

// ログ出力用のマクロ定義
#define LOG_INFO(...)    Logger::instance().log(LogLevel::INFO,    spdlog::source_loc{ __FILE__, __LINE__, SPDLOG_FUNCTION }, __VA_ARGS__)
#define LOG_WARNING(...) Logger::instance().log(LogLevel::WARNING, spdlog::source_loc{ __FILE__, __LINE__, SPDLOG_FUNCTION }, __VA_ARGS__)
#define LOG_ERROR(...)   Logger::instance().log(LogLevel::ERROR,   spdlog::source_loc{ __FILE__, __LINE__, SPDLOG_FUNCTION }, __VA_ARGS__)