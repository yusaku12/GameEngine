#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>
#include "Assets\Animation\AnimationTypes.h"
#include "Assets\Material\MaterialAsset.h"
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
        std::string name;                          //!< 再Import時の対応付けに使用するSlot名
        AssetGUID defaultMaterialGuid{};           //!< 既定Material AssetのGUID
        std::filesystem::path defaultMaterialPath; //!< 既定Material Assetの保存Path
    };

    /**
     * @brief モデル全体のCPU側リソース。
     * FBX SDKおよびDirectX 12の型を保持せず、シリアライズ可能な独自データだけを管理する。
     */
    struct ModelResource
    {
        std::vector<MeshResource> meshes;                      //!< メッシュの配列。
        std::vector<MaterialResource> materials;               //!< マテリアルの配列。
        std::vector<ModelMaterialSlot> materialSlots;          //!< SubMeshが参照するMaterial Slot。
        std::vector<MaterialAsset> embeddedMaterials;          //!< Model Asset内に保存するSlot別Material。
        std::optional<SkeletonResource> skeleton;              //!< スケルトンリソース。
        std::vector<AnimationResource> animations;             //!< アニメーションの配列。
        AssetGUID skeletonAssetGuid{};                         //!< 分離済みSkeleton AssetのGUID。
        std::vector<AssetGUID> animationClipGuids;             //!< 分離済みAnimation Clip AssetのGUID。
        std::vector<std::filesystem::path> animationClipPaths; //!< 外部Animation ClipのAsset Path。
        std::vector<ModelNode> nodes;                          //!< モデルノードの配列。
        AABB boundingBox{};                                    //!< バウンディングボックス。
        BoundingSphere boundingSphere{};                       //!< バウンディングスフィア。
        std::filesystem::path sourcePath;                      //!< モデルのソースファイルパス。

        /**
         * @brief Remove a clip reference and its embedded tracks, preserving their index correspondence.
         * @param guid Clip to remove.
         * @return False without changes if the clip is missing or the arrays are inconsistent.
         * @thread_safety Not thread-safe. Access must be synchronized externally.
         */
        bool removeAnimationClip(const AssetGUID& guid)
        {
            if (!guid.isValid() || animationClipGuids.size() != animationClipPaths.size()
                || animations.size() > animationClipGuids.size())
                return false;
            const auto found = std::find(animationClipGuids.begin(), animationClipGuids.end(), guid);
            if (found == animationClipGuids.end())
                return false;
            const auto index = found - animationClipGuids.begin();
            if (static_cast<std::size_t>(index) < animations.size())
                animations.erase(animations.begin() + index);
            animationClipPaths.erase(animationClipPaths.begin() + index);
            animationClipGuids.erase(found);
            return true;
        }
    };
} // namespace Engine
