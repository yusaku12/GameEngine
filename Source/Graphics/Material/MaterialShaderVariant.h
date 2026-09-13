#pragma once

#include <array>
#include <optional>
#include "Assets\Material\MaterialTypes.h"

namespace Engine
{
    using ShaderVariantID = std::uint8_t;

    /**
     * @brief Standard Shaderが明示的に生成する有限Variant。
     */
    struct MaterialShaderVariant
    {
        ShaderVariantID id;           //!< Variant ID
        MaterialKeywordMask keywords; //!< 有効なKeywordの組み合わせ
    };

    /**
     * @brief Standard Shaderで有効なKeyword組み合わせ。
     */
    inline constexpr std::array STANDARD_SHADER_VARIANTS = {
        MaterialShaderVariant{ 0, 0 },
        MaterialShaderVariant{ 1, toMask(MaterialKeyword::UseNormalMap) },
        MaterialShaderVariant{ 2, toMask(MaterialKeyword::UseEmissiveMap) },
        MaterialShaderVariant{ 3, toMask(MaterialKeyword::UseNormalMap) | toMask(MaterialKeyword::UseEmissiveMap) },
        MaterialShaderVariant{ 4, toMask(MaterialKeyword::UseSkinning) },
        MaterialShaderVariant{ 5, toMask(MaterialKeyword::UseSkinning) | toMask(MaterialKeyword::UseNormalMap) },
        MaterialShaderVariant{ 6, toMask(MaterialKeyword::UseSkinning) | toMask(MaterialKeyword::UseEmissiveMap) },
        MaterialShaderVariant{ 7, toMask(MaterialKeyword::UseSkinning) | toMask(MaterialKeyword::UseNormalMap) | toMask(MaterialKeyword::UseEmissiveMap) },
        MaterialShaderVariant{ 8, toMask(MaterialKeyword::UseAlphaTest) },
        MaterialShaderVariant{ 9, toMask(MaterialKeyword::UseAlphaTest) | toMask(MaterialKeyword::UseNormalMap) },
        MaterialShaderVariant{ 10, toMask(MaterialKeyword::UseAlphaTest) | toMask(MaterialKeyword::UseEmissiveMap) },
        MaterialShaderVariant{ 11, toMask(MaterialKeyword::UseAlphaTest) | toMask(MaterialKeyword::UseNormalMap) | toMask(MaterialKeyword::UseEmissiveMap) },
    };

    /**
     * @brief 明示登録済みのKeyword組み合わせをVariant IDへ解決する。
     * @param keywords 検索するKeyword組み合わせ
     */
    constexpr std::optional<ShaderVariantID> findStandardShaderVariant(const MaterialKeywordMask keywords) noexcept
    {
        for (const MaterialShaderVariant& variant : STANDARD_SHADER_VARIANTS)
        {
            if (variant.keywords == keywords)
                return variant.id;
        }
        return std::nullopt;
    }
} // namespace Engine