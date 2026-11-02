#include "Pch.h"
#include "Core\System\StartupOptions.h"

namespace Engine
{
    bool StartupOptions::parse(const std::span<const std::wstring_view> arguments, std::string& error)
    {
        *this = {};
        error.clear();
        bool runtimeSeen = false;
        bool sceneSeen = false;
        bool validationSeen = false;
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            const auto argument = arguments[index];
            if (argument == L"--runtime" && !runtimeSeen)
            {
                runtimeSeen = true;
                mode = ExecutionMode::Runtime;
            }
            else if (argument == L"--scene" && !sceneSeen)
            {
                sceneSeen = true;
                if (++index == arguments.size() || arguments[index].empty()
                    || arguments[index].starts_with(L"--"))
                {
                    error = "--scene requires a scene file path.";
                    return false;
                }
                scenePath = arguments[index];
            }
            else if (argument == L"--validate-rendering" && !validationSeen)
            {
                validationSeen = true;
                if (++index == arguments.size() || arguments[index].empty()
                    || arguments[index].starts_with(L"--"))
                {
                    error = "--validate-rendering requires an output directory.";
                    return false;
                }
                renderingValidationDirectory = arguments[index];
            }
            else if ((argument == L"--help" || argument == L"-h") && arguments.size() == 1)
                showHelp = true;
            else
            {
                error = "Unknown or duplicate argument. Use --help for usage.";
                return false;
            }
        }
        if (validationSeen)
        {
            if (runtimeSeen || sceneSeen)
            {
                error = "--validate-rendering cannot be combined with --runtime or --scene.";
                return false;
            }
            mode = ExecutionMode::Runtime;
            return true;
        }
        if (mode == ExecutionMode::Runtime && !sceneSeen)
        {
            error = "Runtime requires --scene <scene file>.";
            return false;
        }
        if (sceneSeen && mode != ExecutionMode::Runtime)
        {
            error = "--scene requires --runtime.";
            return false;
        }
        return true;
    }
}
