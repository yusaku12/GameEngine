#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Engine
{
    /**
     * @brief HLSL Shader 段階 (Vertex, Pixel, Compute 等) を表す列挙型
     */
    enum class ShaderStage
    {
        Vertex,         //!< Vertex Shader (vs_6_x)
        Pixel,          //!< Pixel Shader (ps_6_x)
        Compute,        //!< Compute Shader (cs_6_x)
        Geometry,       //!< Geometry Shader (gs_6_x)
        Hull,           //!< Hull Shader (hs_6_x)
        Domain,         //!< Domain Shader (ds_6_x)
        Mesh,           //!< Mesh Shader (ms_6_x)
        Amplification,  //!< Amplification Shader (as_6_x)
        Library         //!< Raytracing Library (lib_6_x)
    };

    /**
     * @brief DXC の HLSL 言語バージョン仕様 (-HV)
     */
    enum class HlslLanguageVersion
    {
        Hlsl2016,   //!< HLSL 2016
        Hlsl2017,   //!< HLSL 2017
        Hlsl2018,   //!< HLSL 2018
        Hlsl2021,   //!< HLSL 2021 (デフォルト)
        Latest      //!< 使用中の DXC がサポートする最新バージョン
    };

    /**
     * @brief DirectX 12 で使用する Shader Model (6.x)
     */
    enum class ShaderModel
    {
        SM_6_0,     //!< Shader Model 6.0
        SM_6_1,     //!< Shader Model 6.1
        SM_6_2,     //!< Shader Model 6.2
        SM_6_3,     //!< Shader Model 6.3
        SM_6_4,     //!< Shader Model 6.4
        SM_6_5,     //!< Shader Model 6.5
        SM_6_6,     //!< Shader Model 6.6
        SM_6_7,     //!< Shader Model 6.7
        SM_6_8,     //!< Shader Model 6.8
        SM_6_9      //!< Shader Model 6.9
    };

    /**
     * @brief Shader Stage に対応する DXC ターゲット接頭辞 (vs, ps, cs 等) を取得する
     * @param stage 取得対象の Shader Stage
     * @return ターゲット接頭辞文字列
     */
    const char* shaderStagePrefix(ShaderStage stage) noexcept;

    /**
     * @brief Shader Stage と Shader Model から DXC Target Profile 文字列 (例: "vs_6_0") を生成する
     * @param stage Shader Stage
     * @param model Shader Model
     * @return DXC Target Profile 文字列
     */
    std::string shaderTargetProfile(ShaderStage stage, ShaderModel model);

    /**
     * @brief DXC コンパイルに必要な設定情報を保持する構造体
     */
    struct ShaderCompileDesc
    {
        std::filesystem::path sourcePath;                                    //!< HLSL ソースファイルのパス
        std::filesystem::path outputPath;                                    //!< コンパイル済み CSO ファイルの出力パス
        std::string entryPoint = "main";                                     //!< Shader エントリーポイント名
        ShaderStage stage = ShaderStage::Vertex;                             //!< Shader Stage
        ShaderModel shaderModel = ShaderModel::SM_6_0;                       //!< 要求 Shader Model
        HlslLanguageVersion languageVersion = HlslLanguageVersion::Hlsl2021; //!< HLSL 言語バージョン
        std::vector<std::filesystem::path> includeDirectories;               //!< インクルードディレクトリ一覧
        std::vector<std::pair<std::string, std::string>> defines;            //!< プリプロセッサマクロ定義 (名前, 値)
        bool debug = false;                                                  //!< デバッグ情報を埋め込むか
        bool optimize = true;                                                //!< 最適化 (-O3) を有効にするか
    };
} // namespace Engine