#pragma once

#include "ShaderTypes.h"
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief HLSL Shader のロード設定
     */
    struct DX12ShaderConfig
    {
        std::filesystem::path bytecodePath; //!< コンパイル済み CSO ファイルのパス
    };

    /**
     * @brief コンパイル済み HLSL Bytecode (.cso) を所有および参照管理するクラス
     * @details Runtime では HLSL コンパイルを行わず、事前またはバックグラウンドでビルドされた .cso をバイナリとして読み込む。
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12Shader
    {
    public:

        DX12Shader() = default;
        ~DX12Shader() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12Shader);

        /**
         * @brief コンパイル済み CSO ファイルから Shader Bytecode を読み込む
         * @param config Shader のロード設定 (CSO パス)
         * @return 読み込みに成功した場合は true
         */
        bool load(const DX12ShaderConfig& config);

        /**
         * @brief 保持しているコンパイル済み Bytecode を解放する
         */
        void finalize();

        /**
         * @brief Pipeline State 作成に使用する Shader Bytecode 構造体を取得する
         * @return D3D12_SHADER_BYTECODE 構造体。未コンパイル時は空の Bytecode
         */
        D3D12_SHADER_BYTECODE getBytecode() const noexcept;

        /**
         * @brief Shader Bytecode のポインタを取得する
         * @return Bytecode ポインタ。未読み込み時は nullptr
         */
        const void* getBytecodePointer() const noexcept { return m_bytecode.data(); }

        /**
         * @brief Shader Bytecode のバイトサイズを取得する
         * @return Bytecode のサイズ (バイト数)
         */
        std::size_t getBytecodeSize() const noexcept { return m_bytecode.size(); }

        /**
         * @brief コンパイル済み (Bytecode 読み込み済み) かを取得する
         * @return 読み込み済みの場合は true
         */
        bool isCompiled() const noexcept { return !m_bytecode.empty(); }

    private:

        std::vector<std::uint8_t> m_bytecode; //!< コンパイル済み Shader Bytecode
    };
} // namespace Engine
