#pragma once

#include <filesystem>
#include <string>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Core\CoreDefines.h"

namespace Engine
{
	/**
	 * @brief HLSL Shader のコンパイル設定
	 */
	struct DX12ShaderConfig
	{
		std::filesystem::path sourcePath; //!< HLSL ファイルのパス
		std::string entryPoint = "main"; //!< エントリーポイント
		std::string targetProfile; //!< Shader Model を含むコンパイルターゲット
	};

	/**
	 * @brief コンパイル済み HLSL Bytecode を所有するクラス
	 * @details Shader は初期化時または明示的な再コンパイル時にのみコンパイルし、Render Loop 内でコンパイルしない。
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12Shader
	{
	public:

		DX12Shader() = default;
		~DX12Shader() = default;

		GE_DISABLE_COPY_AND_MOVE(DX12Shader);

		/**
		 * @brief HLSL ファイルをコンパイルして Bytecode を生成する
		 * @param config Shader のコンパイル設定
		 * @return コンパイルに成功した場合は true
		 */
		bool compile(const DX12ShaderConfig& config);

		/**
		 * @brief 保持しているコンパイル済み Bytecode を解放する
		 */
		void finalize();

		/**
		 * @brief Pipeline State 作成に使用する Shader Bytecode を取得する
		 * @return Shader Bytecode。未コンパイル時は空の Bytecode
		 */
		[[nodiscard]] D3D12_SHADER_BYTECODE getBytecode() const noexcept;

		/**
		 * @brief コンパイル済みかを取得する
		 * @return コンパイル済みの場合は true
		 */
		[[nodiscard]] bool isCompiled() const noexcept { return m_bytecode != nullptr; }

	private:

		Microsoft::WRL::ComPtr<ID3DBlob> m_bytecode; //!< コンパイル済み Shader Bytecode
	};
} // namespace Engine
