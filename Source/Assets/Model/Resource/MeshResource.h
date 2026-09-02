#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Core\Math\Geometry.h"

namespace Engine
{
    /**
     * @brief スキニングに使う最大ボーン影響数。
     */
    inline constexpr std::size_t MAX_BONE_INFLUENCES = 4;

    /**
     * @brief モデル頂点のCPU側データ。
     */
    struct ModelVertex
    {
        Vector3 position = Vector3(0.0f, 0.0f, 0.0f);                 //!< 頂点座標
        Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);                   //!< 法線ベクトル
        Vector3 tangent = Vector3(1.0f, 0.0f, 0.0f);                  //!< 接線ベクトル
        Vector3 bitangent = Vector3(0.0f, 0.0f, 1.0f);                //!< 接線ベクトルに垂直なベクトル
        Vector2 texCoord = Vector2(0.0f, 0.0f);                       //!< UV座標
        Vector4 color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);              //!< 頂点カラー
        std::array<std::uint16_t, MAX_BONE_INFLUENCES> boneIndices{}; //!< ボーンインデックス
        Vector4 boneWeights = Vector4(0.0f, 0.0f, 0.0f, 0.0f);        //!< ボーンウェイト
    };

    /**
     * @brief メッシュ内の描画単位。
     */
    struct SubMeshResource
    {
        std::uint32_t indexStart = 0;    //!< インデックスバッファの開始位置
        std::uint32_t indexCount = 0;    //!< インデックスバッファの数
        std::uint32_t materialIndex = 0; //!< マテリアルインデックス
    };

    /**
     * @brief モデルのCPU側メッシュデータ。
     */
    struct MeshResource
    {
        std::string name;                       //!< メッシュ名
        std::vector<ModelVertex> vertices;      //!< 頂点バッファ
        std::vector<std::uint32_t> indices;     //!< インデックスバッファ
        std::vector<SubMeshResource> subMeshes; //!< サブメッシュ情報
        AABB boundingBox{};                     //!< バウンディングボックス
        BoundingSphere boundingSphere{};        //!< バウンディングスフィア
    };
} // namespace Engine
