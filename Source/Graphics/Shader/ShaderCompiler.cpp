#include "Pch.h"
#include "ShaderCompiler.h"

namespace Engine
{
    namespace
    {
        std::string quote(const std::filesystem::path& value) { return "\"" + value.string() + "\""; }

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
        if (desc.sourcePath.empty() || desc.outputPath.empty() || desc.entryPoint.empty())
        {
            result.diagnostics = "ShaderCompileDesc is incomplete";
            return result;
        }

        std::error_code error;
        std::filesystem::create_directories(desc.outputPath.parent_path(), error);
        std::string command = quote(m_dxcPath) + " -HV " + languageVersion(desc.languageVersion)
            + " -E " + desc.entryPoint + " -T " + shaderTargetProfile(desc.stage, desc.shaderModel)
            + " -Fo " + quote(desc.outputPath) + (desc.debug ? " -Zi -Qembed_debug" : "")
            + (desc.optimize ? " -O3" : " -Od");
        for (const auto& includeDirectory : desc.includeDirectories)
            command += " -I " + quote(includeDirectory);
        for (const auto& [name, value] : desc.defines)
            command += " -D" + name + (value.empty() ? "" : "=" + value);
        command += " " + quote(desc.sourcePath);

        LOG_INFO("[ShaderCompiler] Compiling {} ({})", desc.sourcePath.string(), shaderTargetProfile(desc.stage, desc.shaderModel));
        result.success = std::system(command.c_str()) == 0 && std::filesystem::exists(desc.outputPath);
        if (!result.success)
            result.diagnostics = "DXC compilation failed. Command: " + command;
        return result;
    }
} // namespace Engine