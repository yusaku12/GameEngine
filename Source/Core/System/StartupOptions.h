#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include "Core\System\ExecutionMode.h"

namespace Engine
{
    /** @brief Validated process startup configuration; no distribution/build settings. */
    struct StartupOptions
    {
        ExecutionMode mode = ExecutionMode::Editor;
        std::filesystem::path scenePath;
        bool showHelp = false;

        /**
         * @brief Parses arguments excluding the executable name.
         * @param arguments Windows-decoded argument strings.
         * @param error Receives a diagnostic on invalid arguments.
         * @return True for valid editor, Runtime or help arguments.
         */
        bool parse(std::span<const std::wstring_view> arguments, std::string& error);
    };
}
