#include "Pch.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void Logger::initialize(const std::string& fileName)
{
    if (m_logger)
        return;

    try
    {
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern("[%T.%e] [%^%-7l%$] %v");

        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fileName, true);
        file->set_pattern("[%Y-%m-%d %T.%e] [%-7l] %v (%s:%#)");

        auto debugOutput = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        debugOutput->set_pattern("[%-7l] %v (%s:%#)");

        std::vector<spdlog::sink_ptr> sinks{ console, file, debugOutput };
        m_logger = std::make_shared<spdlog::logger>("GameEngine", sinks.begin(), sinks.end());
        m_logger->set_level(spdlog::level::trace);
        m_logger->flush_on(spdlog::level::warn);
    }
    catch (const spdlog::spdlog_ex& e)
    {
        m_logger.reset();
        std::fprintf(stderr, "Failed to initialize logger: %s\n", e.what());
    }
}

void Logger::finalize()
{
    if (!m_logger)
        return;

    m_logger->flush();
    m_logger.reset();
}

spdlog::level::level_enum Logger::toSpdlogLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::INFO:    return spdlog::level::info;
    case LogLevel::WARNING: return spdlog::level::warn;
    case LogLevel::ERROR:   return spdlog::level::err;
    default:                return spdlog::level::info;
    }
}