#include "Pch.h"

#include "Core\Logging\Assert.h"

namespace Engine
{
    bool assertFailed(const char* expression, std::string_view message, spdlog::source_loc loc)
    {
        if (message.empty())
            Logger::instance().log(LogLevel::CRITICAL, loc, "アサート失敗: {}", expression);
        else
            Logger::instance().log(LogLevel::CRITICAL, loc, "アサート失敗: {} ({})", message, expression);

        Logger::instance().flush();

        if (::IsDebuggerPresent())
            GE_DEBUG_BREAK();

        return false;
    }

    void fatalError(std::string_view message, spdlog::source_loc loc)
    {
        Logger::instance().log(LogLevel::CRITICAL, loc, "致命的なエラー: {}", message);
        Logger::instance().flush();

        if (::IsDebuggerPresent())
            GE_DEBUG_BREAK();

        std::abort();
    }
} // namespace Engine