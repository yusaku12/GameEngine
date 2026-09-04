#pragma once

#include "ShaderTypes.h"

namespace Engine
{
    /**
     * @brief DXC Shader コンパイル結果を保持する構造体
     */
    struct ShaderCompileResult
    {
        bool success = false;             //!< コンパイルが成功したかどうか
        std::string diagnostics;          //!< DXC の出力する警告・エラー診断メッセージ
        std::filesystem::path outputPath; //!< コンパイル成果物 (.cso) のパス
    };

    /**
     * @brief DXC (DirectX Shader Compiler) を使用した Shader コンパイルを実行・抽象化するクラス
     * @details Runtime では使用せず、Editor や Development モード、ツールチェーンでの Shader コンパイルおよび Hot Reload 時のバックグラウンド再ビルドに使用する。
     * @thread_safety Thread-safe. コンパイル関数は外部同期なしで安全に呼び出し可能。
     */
    class ShaderCompiler
    {
    public:

        /**
         * @brief ShaderCompiler のコンストラクタ
         * @param dxcPath 使用する DXC CLI (dxc.exe) のパス。デフォルトは "Tools/DXC/dxc.exe"
         */
        explicit ShaderCompiler(std::filesystem::path dxcPath = "Tools/DXC/dxc.exe");

        /**
         * @brief 設定情報に従って DXC を呼び出し、HLSL を CSO へコンパイルする
         * @param desc Shader コンパイル設定 (ソースパス、出力パス、ステージ、エントリーポイント等)
         * @return コンパイル結果 (成功判定、ログ、出力パス)
         */
        ShaderCompileResult compile(const ShaderCompileDesc& desc) const;

        /**
         * @brief 設定されている DXC CLI のパスを取得する
         * @return DXC CLI のパス参照
         */
        const std::filesystem::path& getDxcPath() const noexcept { return m_dxcPath; }

    private:

        std::filesystem::path m_dxcPath; //!< DXC CLI (dxc.exe) の実行パス
    };
} // namespace Engine