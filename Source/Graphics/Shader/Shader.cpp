#include "Pch.h"
#include "Shader.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    bool DX12Shader::load(const DX12ShaderConfig& config)
    {
        if (config.bytecodePath.empty())
        {
            LOG_ERROR("[DX12] Shader のコンパイル設定が不正です");
            return false;
        }

        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(config.bytecodePath, filesystemError))
        {
            if (filesystemError)
            {
                LOG_ERROR("[Shader] CSO ファイルの確認に失敗しました: {} ({})",
                    config.bytecodePath.string(), filesystemError.message());
                return false;
            }
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
        {
            LOG_ERROR("[Shader] CSO のサイズが不正です: {}", config.bytecodePath.string());
            return false;
        }
        std::vector<std::uint8_t> bytecode(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytecode.data()), size);
        if (!file)
        {
            LOG_ERROR("[Shader] CSO の読み込みに失敗しました: {}", config.bytecodePath.string());
            return false;
        }
        m_bytecode = std::move(bytecode);
        return true;
    }

    void DX12Shader::finalize()
    {
        m_bytecode.clear();
    }

    bool DX12Shader::loadBytecode(const std::span<const std::uint8_t> bytecode)
    {
        if (bytecode.empty())
        {
            LOG_ERROR("[Shader] Shader bytecode is empty.");
            return false;
        }
        try
        {
            std::vector<std::uint8_t> copy(bytecode.begin(), bytecode.end());
            m_bytecode = std::move(copy);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[Shader] Cannot allocate shader bytecode.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[Shader] Shader bytecode exceeds available capacity.");
        }
        return false;
    }

    D3D12_SHADER_BYTECODE DX12Shader::getBytecode() const noexcept
    {
        return D3D12_SHADER_BYTECODE{
            .pShaderBytecode = m_bytecode.empty() ? nullptr : m_bytecode.data(),
            .BytecodeLength = m_bytecode.size(),
        };
    }
} // namespace Engine