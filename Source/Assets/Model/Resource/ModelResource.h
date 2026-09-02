#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include "Assets\Model\Resource\AnimationResource.h"
#include "Assets\Model\Resource\MaterialResource.h"
#include "Assets\Model\Resource\MeshResource.h"
#include "Assets\Model\Resource\ModelNode.h"
#include "Assets\Model\Resource\SkeletonResource.h"

namespace Engine
{
    /**
     * @brief モデル全体のCPU側リソース。
     * AssimpおよびDirectX 12の型を保持せず、シリアライズ可能な独自データだけを管理する。
     */
    struct ModelResource
    {
        std::vector<MeshResource> meshes;          //!< メッシュの配列。
        std::vector<MaterialResource> materials;   //!< マテリアルの配列。
        std::optional<SkeletonResource> skeleton;  //!< スケルトンリソース。
        std::vector<AnimationResource> animations; //!< アニメーションの配列。
        std::vector<ModelNode> nodes;              //!< モデルノードの配列。
        AABB boundingBox{};                        //!< バウンディングボックス。
        BoundingSphere boundingSphere{};           //!< バウンディングスフィア。
        std::filesystem::path sourcePath;          //!< モデルのソースファイルパス。
    };
} // namespace Engine
