#include "Pch.h"
#include "Graphics\DirectX12\Shader.h"

namespace Engine
{
	bool DX12Shader::compile(const DX12ShaderConfig& config)
	{
		if (config.sourcePath.empty() || config.entryPoint.empty() || config.targetProfile.empty())
		{
			LOG_ERROR("[DX12] Shader のコンパイル設定が不正です");
			return false;
		}

		if (!std::filesystem::exists(config.sourcePath))
		{
			LOG_ERROR("[DX12] Shader ファイルが見つかりません: {}", config.sourcePath.string());
			return false;
		}

		UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
		compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

		Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		const HRESULT result = D3DCompileFromFile(
			config.sourcePath.c_str(),
			nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			config.entryPoint.c_str(),
			config.targetProfile.c_str(),
			compileFlags,
			0,
			&bytecode,
			&errors);
		if (FAILED(result))
		{
			if (errors != nullptr)
			{
				const std::string errorMessage(
					static_cast<const char*>(errors->GetBufferPointer()),
					errors->GetBufferSize());
				LOG_ERROR("[DX12] Shader のコンパイルに失敗しました: {}", errorMessage);
			}
			else
			{
				LOG_ERROR("[DX12] Shader のコンパイルに失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
			}
			return false;
		}

		m_bytecode = std::move(bytecode);
		return true;
	}

	void DX12Shader::finalize()
	{
		m_bytecode.Reset();
	}

	D3D12_SHADER_BYTECODE DX12Shader::getBytecode() const noexcept
	{
		return D3D12_SHADER_BYTECODE{
			.pShaderBytecode = m_bytecode != nullptr ? m_bytecode->GetBufferPointer() : nullptr,
			.BytecodeLength = m_bytecode != nullptr ? m_bytecode->GetBufferSize() : 0,
		};
	}
} // namespace Engine