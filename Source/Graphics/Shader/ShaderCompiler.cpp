#include "Pch.h"
#include "ShaderCompiler.h"

namespace Engine
{
    namespace
    {
        std::wstring quoteArgument(const std::wstring& argument)
        {
            std::wstring quoted;
            quoted.reserve(argument.size() + 2);
            quoted.push_back(L'"');
            std::size_t backslashes = 0;
            for (const wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (character == L'"')
                {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(character);
                    backslashes = 0;
                    continue;
                }
                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(character);
            }
            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }

        bool isIdentifier(const std::string& value) noexcept
        {
            if (value.empty())
                return false;
            const auto isStart = [](const unsigned char character)
                {
                    return (character >= 'A' && character <= 'Z')
                        || (character >= 'a' && character <= 'z') || character == '_';
                };
            const auto isContinue = [&isStart](const unsigned char character)
                {
                    return isStart(character) || (character >= '0' && character <= '9');
                };
            if (!isStart(static_cast<unsigned char>(value.front())))
                return false;
            return std::all_of(value.begin() + 1, value.end(), [&isContinue](const char character)
                {
                    return isContinue(static_cast<unsigned char>(character));
                });
        }

        bool isValidStage(const ShaderStage stage) noexcept
        {
            switch (stage)
            {
            case ShaderStage::Vertex:
            case ShaderStage::Pixel:
            case ShaderStage::Compute:
            case ShaderStage::Geometry:
            case ShaderStage::Hull:
            case ShaderStage::Domain:
            case ShaderStage::Mesh:
            case ShaderStage::Amplification:
            case ShaderStage::Library:
                return true;
            }
            return false;
        }

        bool isValidModel(const ShaderModel model) noexcept
        {
            switch (model)
            {
            case ShaderModel::SM_6_0:
            case ShaderModel::SM_6_1:
            case ShaderModel::SM_6_2:
            case ShaderModel::SM_6_3:
            case ShaderModel::SM_6_4:
            case ShaderModel::SM_6_5:
            case ShaderModel::SM_6_6:
            case ShaderModel::SM_6_7:
            case ShaderModel::SM_6_8:
            case ShaderModel::SM_6_9:
                return true;
            }
            return false;
        }

        bool isValidLanguageVersion(const HlslLanguageVersion version) noexcept
        {
            switch (version)
            {
            case HlslLanguageVersion::Hlsl2016:
            case HlslLanguageVersion::Hlsl2017:
            case HlslLanguageVersion::Hlsl2018:
            case HlslLanguageVersion::Hlsl2021:
            case HlslLanguageVersion::Latest:
                return true;
            }
            return false;
        }

        const char* languageVersion(const HlslLanguageVersion version) noexcept
        {
            switch (version)
            {
            case HlslLanguageVersion::Hlsl2016: return "2016";
            case HlslLanguageVersion::Hlsl2017: return "2017";
            case HlslLanguageVersion::Hlsl2018: return "2018";
            case HlslLanguageVersion::Hlsl2021:
            case HlslLanguageVersion::Latest: return "2021";
            }
            return "2021";
        }
    }

    const char* shaderStagePrefix(const ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case ShaderStage::Vertex: return "vs";
        case ShaderStage::Pixel: return "ps";
        case ShaderStage::Compute: return "cs";
        case ShaderStage::Geometry: return "gs";
        case ShaderStage::Hull: return "hs";
        case ShaderStage::Domain: return "ds";
        case ShaderStage::Mesh: return "ms";
        case ShaderStage::Amplification: return "as";
        case ShaderStage::Library: return "lib";
        }
        return "lib";
    }

    std::string shaderTargetProfile(const ShaderStage stage, const ShaderModel model)
    {
        return std::string(shaderStagePrefix(stage)) + "_6_" + std::to_string(static_cast<int>(model));
    }

    ShaderCompiler::ShaderCompiler(std::filesystem::path dxcPath)
        : m_dxcPath(std::move(dxcPath))
    {
    }

    ShaderCompileResult ShaderCompiler::compile(const ShaderCompileDesc& desc) const
    {
        ShaderCompileResult result{ .outputPath = desc.outputPath };
        if (m_dxcPath.empty() || desc.sourcePath.empty() || desc.outputPath.empty()
            || !isIdentifier(desc.entryPoint) || !isValidStage(desc.stage)
            || !isValidModel(desc.shaderModel) || !isValidLanguageVersion(desc.languageVersion))
        {
            result.diagnostics = "ShaderCompiler path or ShaderCompileDesc is invalid";
            return result;
        }
        for (const auto& [name, value] : desc.defines)
        {
            GE_UNUSED(value);
            if (!isIdentifier(name))
            {
                result.diagnostics = "Shader macro name is invalid";
                return result;
            }
        }

        std::error_code error;
        const std::filesystem::path outputDirectory = desc.outputPath.parent_path();
        if (!outputDirectory.empty())
        {
            std::filesystem::create_directories(outputDirectory, error);
            if (error)
            {
                result.diagnostics = "Could not create shader output directory: " + error.message();
                return result;
            }
        }

        std::vector<std::wstring> arguments{
            m_dxcPath.wstring(),
            L"-HV", std::wstring(languageVersion(desc.languageVersion), languageVersion(desc.languageVersion) + std::strlen(languageVersion(desc.languageVersion))),
            L"-E", std::filesystem::path(desc.entryPoint).wstring(),
            L"-T", std::filesystem::path(shaderTargetProfile(desc.stage, desc.shaderModel)).wstring(),
            L"-Fo", desc.outputPath.wstring(),
        };
        if (desc.debug)
        {
            arguments.emplace_back(L"-Zi");
            arguments.emplace_back(L"-Qembed_debug");
        }
        arguments.emplace_back(desc.optimize ? L"-O3" : L"-Od");
        for (const auto& includeDirectory : desc.includeDirectories)
        {
            arguments.emplace_back(L"-I");
            arguments.push_back(includeDirectory.wstring());
        }
        for (const auto& [name, value] : desc.defines)
        {
            std::wstring definition = L"-D" + std::filesystem::path(name).wstring();
            if (!value.empty())
            {
                const std::wstring wideValue = std::filesystem::path(value).wstring();
                definition += L"=" + wideValue;
            }
            arguments.push_back(std::move(definition));
        }
        arguments.push_back(desc.sourcePath.wstring());

        std::wstring commandLine;
        for (const std::wstring& argument : arguments)
        {
            if (!commandLine.empty())
                commandLine.push_back(L' ');
            commandLine += quoteArgument(argument);
        }

        const std::wstring executable = m_dxcPath.wstring();
        LOG_INFO("[ShaderCompiler] Compiling {} ({})", desc.sourcePath.string(), shaderTargetProfile(desc.stage, desc.shaderModel));
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        SECURITY_ATTRIBUTES pipeSecurity{ .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE };
        HANDLE outputRead = nullptr;
        HANDLE outputWrite = nullptr;
        if (!CreatePipe(&outputRead, &outputWrite, &pipeSecurity, 0)
            || !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0))
        {
            const DWORD errorCode = GetLastError();
            if (outputRead != nullptr)
                CloseHandle(outputRead);
            if (outputWrite != nullptr)
                CloseHandle(outputWrite);
            result.diagnostics = "Could not create DXC output pipe (Windows error: " + std::to_string(errorCode) + ")";
            return result;
        }

        HANDLE inputHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &pipeSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (inputHandle == INVALID_HANDLE_VALUE)
        {
            const DWORD errorCode = GetLastError();
            CloseHandle(outputRead);
            CloseHandle(outputWrite);
            result.diagnostics = "Could not open NUL for DXC standard input (Windows error: "
                + std::to_string(errorCode) + ")";
            return result;
        }

        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = outputWrite;
        startupInfo.hStdError = outputWrite;
        startupInfo.hStdInput = inputHandle;
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
        {
            const DWORD errorCode = GetLastError();
            CloseHandle(outputRead);
            CloseHandle(outputWrite);
            CloseHandle(inputHandle);
            result.diagnostics = "Could not start DXC (Windows error: " + std::to_string(errorCode) + ")";
            return result;
        }

        CloseHandle(outputWrite);
        CloseHandle(inputHandle);
        CloseHandle(processInfo.hThread);
        std::array<char, 4096> outputBuffer{};
        DWORD bytesRead = 0;
        DWORD readError = ERROR_SUCCESS;
        while (true)
        {
            if (!ReadFile(outputRead, outputBuffer.data(), static_cast<DWORD>(outputBuffer.size()), &bytesRead, nullptr))
            {
                readError = GetLastError();
                break;
            }
            if (bytesRead == 0)
                break;
            result.diagnostics.append(outputBuffer.data(), bytesRead);
        }
        CloseHandle(outputRead);
        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 0;
        const bool gotExitCode = waitResult == WAIT_OBJECT_0
            && GetExitCodeProcess(processInfo.hProcess, &exitCode);
        const DWORD processError = gotExitCode ? ERROR_SUCCESS : GetLastError();
        CloseHandle(processInfo.hProcess);
        if (!gotExitCode)
        {
            result.diagnostics += "\nFailed while waiting for DXC (Windows error: " + std::to_string(processError) + ")";
            return result;
        }
        result.success = exitCode == 0 && std::filesystem::is_regular_file(desc.outputPath, error) && !error;
        if (!result.success)
            result.diagnostics += "\nDXC compilation failed with exit code " + std::to_string(exitCode);
        if (readError != ERROR_BROKEN_PIPE && readError != ERROR_SUCCESS)
            result.diagnostics += "\nFailed to read DXC output (Windows error: " + std::to_string(readError) + ")";
        return result;
    }
} // namespace Engine