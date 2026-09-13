#pragma once

#include <string>
#include "Assets\Material\MaterialTypes.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief Materialが参照するTexture AssetのGUID。
     */
    struct MaterialTextureReferences
    {
        AssetGUID baseColor;         //!< BaseColor TextureのGUID
        AssetGUID normal;            //!< Normal TextureのGUID
        AssetGUID metallicRoughness; //!< MetallicRoughness TextureのGUID
        AssetGUID ambientOcclusion;  //!< AmbientOcclusion TextureのGUID
        AssetGUID emissive;          //!< Emissive TextureのGUID
    };

    /**
     * @brief 型安全なMaterial描画状態。
     */
    struct MaterialRenderState
    {
        MaterialSurfaceType surfaceType = MaterialSurfaceType::Opaque; //!< Materialの描画分類
        MaterialCullMode cullMode = MaterialCullMode::Back;            //!< カリングモード
        MaterialDepthTest depthTest = MaterialDepthTest::LessEqual;    //!< 深度テストモード
        MaterialBlendMode blendMode = MaterialBlendMode::Opaque;       //!< ブレンドモード
        bool depthWrite = true;                                        //!< 深度書き込みの有無
        std::int32_t renderQueueOffset = 0;                            //!< レンダーキューのオフセット
    };

    /**
     * @brief シリアライズ可能な固定Standard PBR Material Asset。
     * @details GPU API型を保持せず、MaterialManagerから不変Snapshotとして公開する。
     */
    struct MaterialAsset
    {
        AssetGUID guid;                                      //!< Material AssetのGUID
        std::string name;                                    //!< Material Assetの名前
        AssetGUID shaderGuid;                                //!< 使用するShaderのGUID
        MaterialRenderState renderState;                     //!< Materialの描画状態
        Vector4 baseColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f); //!< 基本色
        float metallic = 0.0f;                               //!< 金属度
        float roughness = 1.0f;                              //!< 粗さ
        Vector3 emissiveColor = Vector3::Zero;               //!< エミッシブカラー
        float emissiveIntensity = 0.0f;                      //!< エミッシブ強度
        float normalScale = 1.0f;                            //!< 法線マップのスケール
        float occlusionStrength = 1.0f;                      //!< アンビエントオクルージョンの強度
        float alphaCutoff = 0.5f;                            //!< アルファカットオフ値
        MaterialTextureReferences textures;                  //!< 使用するテクスチャ参照
        MaterialKeywordMask shaderKeywords = 0;              //!< シェーダーキーワードマスク
    };
} // namespace Engine