#pragma once

#include <cstdint>
#include "Core\Object\ObjectGUID.h"

namespace Engine
{
    using AssetGUID = ObjectGUID;

    /**
     * @brief MaterialManagerが管理するMaterial AssetのHandle。
     */
    struct MaterialHandle
    {
        //! 無効なインデックス値
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

        std::uint32_t index = INVALID_INDEX; //!< インデックス値
        std::uint32_t generation = 0;        //!< 世代番号

        /**
         * @brief Handleが有効な場合はtrueを返す
         */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }

        /**
         * @brief 無効なHandleを返す。
         */
        static constexpr MaterialHandle Invalid() noexcept { return {}; }

        friend bool operator==(const MaterialHandle&, const MaterialHandle&) = default;
    };

    /**
     * @brief Materialの描画分類。Opacity値から暗黙に変更しない。
     */
    enum class MaterialSurfaceType : std::uint8_t
    {
        Opaque,      //!< 不透明
        AlphaTest,   //!< アルファテスト
        Transparent, //!< 透明
    };

    /**
     * @brief Materialのカリング方式。
     */
    enum class MaterialCullMode : std::uint8_t
    {
        None,  //!< カリングなし
        Front, //!< 前面カリング
        Back,  //!< 背面カリング
    };

    /**
     * @brief Materialの深度比較方式。
     */
    enum class MaterialDepthTest : std::uint8_t
    {
        Disabled,     //!< 深度テスト無効
        Less,         //!< 深度テスト有効、Less
        LessEqual,    //!< 深度テスト有効、LessEqual
        Equal,        //!< 深度テスト有効、Equal
        GreaterEqual, //!< 深度テスト有効、GreaterEqual
        Greater,      //!< 深度テスト有効、Greater
        Always,       //!< 深度テスト有効、Always
    };

    /**
     * @brief MaterialのBlend方式。
     */
    enum class MaterialBlendMode : std::uint8_t
    {
        Opaque,   //!< 不透明
        Alpha,    //!< アルファブレンド
        Additive, //!< 加算ブレンド
    };

    /**
     * @brief Standard Materialで許可されたShader Keyword。
     */
    enum class MaterialKeyword : std::uint32_t
    {
        None = 0,                 //!< なし
        UseNormalMap = 1u << 0,   //!< 法線マップを使用する
        UseEmissiveMap = 1u << 1, //!< エミッシブマップを使用する
        UseSkinning = 1u << 2,    //!< スキニングを使用する
        UseAlphaTest = 1u << 3,   //!< アルファテストを使用する
    };

    //! Material Keywordのbit mask。
    using MaterialKeywordMask = std::uint32_t;

    //! Material Keywordのbit maskで有効なビット。
    inline constexpr MaterialKeywordMask VALID_MATERIAL_KEYWORDS = (1u << 4) - 1;

    /**
     * @brief Material Keywordをbit maskへ変換する。
     * @param keyword 変換するMaterial Keyword
     */
    constexpr MaterialKeywordMask toMask(const MaterialKeyword keyword) noexcept
    {
        return static_cast<MaterialKeywordMask>(keyword);
    }
} // namespace Engine