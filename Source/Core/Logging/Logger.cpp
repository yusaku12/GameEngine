#include "Pch.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Core\Logging\Logger.h"

namespace Engine
{

bool Logger::initialize(const LoggerConfig& config)
{
    if (m_logger != nullptr)
        return true;

    try
    {
        std::vector<spdlog::sink_ptr> sinks;

        if (config.useConsole)
        {
            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern("[%T.%e] [%^%-8l%$] %v");
            sinks.push_back(console);
        }

        if (config.useFile)
        {
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.fileName, config.truncate);
            file->set_pattern("[%Y-%m-%d %T.%e] [%-8l] [%t] %v (%s:%#)");
            sinks.push_back(file);
        }

        if (config.useDebugOutput)
        {
            auto debugOutput = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            debugOutput->set_pattern("[%-8l] %v (%s:%#)");
            sinks.push_back(debugOutput);
        }

        m_logger = std::make_shared<spdlog::logger>("GameEngine", sinks.begin(), sinks.end());
        m_logger->set_level(spdlog::level::trace);
        m_logger->flush_on(toSpdlogLevel(config.flushLevel));

        m_level = config.level;
    }
    catch (const spdlog::spdlog_ex& exception)
    {
        m_logger.reset();
        std::fprintf(stderr, "Failed to initialize logger: %s\n", exception.what());
        return false;
    }

    return true;
}

void Logger::finalize()
{
    if (m_logger == nullptr)
        return;

    m_logger->flush();
    m_logger.reset();
}

void Logger::setLevel(LogLevel level)
{
    m_level = level;
}

void Logger::flush()
{
    if (m_logger != nullptr)
        m_logger->flush();
}

void Logger::logMessage(LogLevel level, spdlog::source_loc loc, std::string_view message)
{
    if (!isEnabled(level))
        return;

    m_logger->log(loc, toSpdlogLevel(level), "{}", message);
}

spdlog::level::level_enum Logger::toSpdlogLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:    return spdlog::level::trace;
    case LogLevel::DEBUG:    return spdlog::level::debug;
    case LogLevel::INFO:     return spdlog::level::info;
    case LogLevel::WARNING:  return spdlog::level::warn;
    case LogLevel::ERROR:    return spdlog::level::err;
    case LogLevel::CRITICAL: return spdlog::level::critical;
    case LogLevel::OFF:      return spdlog::level::off;
    default:                 return spdlog::level::info;
    }
}

} // namespace Engine
