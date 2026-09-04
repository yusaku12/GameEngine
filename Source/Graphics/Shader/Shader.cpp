#include "Pch.h"
#include "Shader.h"

namespace Engine
{
    bool DX12Shader::load(const DX12ShaderConfig& config)
    {
        if (config.bytecodePath.empty())
        {
            LOG_ERROR("[DX12] Shader のコンパイル設定が不正です");
            return false;
        }

        if (!std::filesystem::exists(config.bytecodePath))
        {
            LOG_ERROR("[Shader] CSO ファイルが見つかりません: {}", config.bytecodePath.string());
            return false;
        }

        std::ifstream file(config.bytecodePath, std::ios::binary | std::ios::ate);
        if (!file)
        {
            LOG_ERROR("[Shader] CSO を開けません: {}", config.bytecodePath.string());
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size <= 0)
            return false;
        m_bytecode.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(m_bytecode.data()), size);
        if (!file)
            return false;
        return true;
    }

    void DX12Shader::finalize()
    {
        m_bytecode.clear();
    }

    D3D12_SHADER_BYTECODE DX12Shader::getBytecode() const noexcept
    {
        return D3D12_SHADER_BYTECODE{
            .pShaderBytecode = m_bytecode.empty() ? nullptr : m_bytecode.data(),
            .BytecodeLength = m_bytecode.size(),
        };
    }
} // namespace Engine