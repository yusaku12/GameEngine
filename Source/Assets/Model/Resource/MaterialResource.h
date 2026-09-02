#pragma once

#include <string>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief モデルが参照するテクスチャのアセットパス。
     */
    struct MaterialTexturePaths
    {
        std::string baseColor;          //!< BaseColorテクスチャのアセットパス。
        std::string normal;             //!< Normalテクスチャのアセットパス。
        std::string metallic;           //!< Metallicテクスチャのアセットパス。
        std::string roughness;          //!< Roughnessテクスチャのアセットパス。
        std::string metallicRoughness;  //!< MetallicRoughnessテクスチャのアセットパス。
        std::string ambientOcclusion;   //!< AmbientOcclusionテクスチャのアセットパス。
        std::string emissive;           //!< Emissiveテクスチャのアセットパス。
        std::string opacity;            //!< Opacityテクスチャのアセットパス。
    };

    /**
     * @brief モデルのCPU側マテリアルデータ。
     * テクスチャはGPUリソースではなく、解決済みのアセットパスだけを保持する。
     */
    struct MaterialResource
    {
        std::string name;                                    //!< マテリアル名。
        Vector4 baseColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f); //!< BaseColor値。
        float metallic = 0.0f;                               //!< Metallic値。
        float roughness = 1.0f;                              //!< Roughness値。
        Vector3 emissive = Vector3(0.0f, 0.0f, 0.0f);        //!< Emissive値。
        float opacity = 1.0f;                                //!< Opacity値。
        MaterialTexturePaths textures;                       //!< テクスチャのアセットパス。
    };
} // namespace Engine
