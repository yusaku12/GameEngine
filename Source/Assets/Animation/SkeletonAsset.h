#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <ozz/animation/runtime/skeleton.h>
#include "Assets\Animation\AnimationTypes.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief Skeletonのアセット情報。
     */
    struct SkeletonAsset
    {
        AssetGUID guid;                                             //!< AssetGUID
        std::string name;                                           //!< Skeleton名
        ozz::animation::Skeleton skeleton;                          //!< ozz Skeleton
        std::vector<std::string> jointNames;                        //!< ジョイント名のリスト
        std::unordered_map<std::string, std::uint32_t> jointLookup; //!< ジョイント名からインデックスへのマッピング
        std::vector<std::int32_t> parentIndices;                    //!< 親ジョイントのインデックスリスト
        std::vector<std::string> hierarchyPaths;                    //!< ジョイントの階層パスリスト
        std::vector<Matrix> inverseBindPoses;                       //!< インバースバインドポーズ行列のリスト
        std::vector<Matrix> localBindTransforms;                    //!< ローカルバインド変換行列のリスト
        SkeletonSignature signature;                                //!< Skeletonの署名
        std::filesystem::path sourcePath;                           //!< ソースファイルのパス
    };
}
