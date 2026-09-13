#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include "Assets\Material\MaterialTypes.h"
#include "Assets\Model\Resource\AnimationResource.h"
#include "Assets\Model\Resource\MaterialResource.h"
#include "Assets\Model\Resource\MeshResource.h"
#include "Assets\Model\Resource\ModelNode.h"
#include "Assets\Model\Resource\SkeletonResource.h"

namespace Engine
{
    /**
     * @brief Model内のSubMeshが参照するMaterial Slot。
     */
    struct ModelMaterialSlot
    {
        std::string name;                 //!< 再Import時の対応付けに使用するSlot名
        AssetGUID defaultMaterialGuid{};  //!< 既定Material AssetのGUID
    };

    /**
     * @brief モデル全体のCPU側リソース。
     * AssimpおよびDirectX 12の型を保持せず、シリアライズ可能な独自データだけを管理する。
     */
    struct ModelResource
    {
        std::vector<MeshResource> meshes;             //!< メッシュの配列。
        std::vector<MaterialResource> materials;      //!< マテリアルの配列。
        std::vector<ModelMaterialSlot> materialSlots; //!< SubMeshが参照するMaterial Slot。
        std::optional<SkeletonResource> skeleton;     //!< スケルトンリソース。
        std::vector<AnimationResource> animations;    //!< アニメーションの配列。
        std::vector<ModelNode> nodes;                 //!< モデルノードの配列。
        AABB boundingBox{};                           //!< バウンディングボックス。
        BoundingSphere boundingSphere{};              //!< バウンディングスフィア。
        std::filesystem::path sourcePath;             //!< モデルのソースファイルパス。
    };
} // namespace Engine
