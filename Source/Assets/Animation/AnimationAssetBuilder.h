#pragma once

#include "Assets\Animation\AnimationClipAsset.h"
#include "Assets\Animation\SkeletonAsset.h"
#include "Assets\Model\Resource\ModelResource.h"

namespace Engine
{
    /**
     * @brief Animation Assetのビルドを行うクラス。
     */
    class AnimationAssetBuilder
    {
    public:

        /**
         * @brief SkeletonAssetをビルドする。
         * @param source SkeletonResource
         * @param sourcePath ソースファイルのパス
         * @param destination ビルド結果のSkeletonAsset
         * @return ビルドに成功した場合はtrue
         */
        bool buildSkeleton(const SkeletonResource& source, const std::filesystem::path& sourcePath, SkeletonAsset& destination) const;

        /**
         * @brief AnimationClipAssetをビルドする。
         * @param source AnimationResource
         * @param skeleton SkeletonAsset
         * @param sourcePath ソースファイルのパス
         * @param destination ビルド結果のAnimationClipAsset
         * @return ビルドに成功した場合はtrue
         */
        bool buildClip(const AnimationResource& source, const SkeletonAsset& skeleton, const std::filesystem::path& sourcePath, AnimationClipAsset& destination) const;

        /**
         * @brief Skeletonの署名を計算する。
         * @param hierarchyPaths ジョイントの階層パスリスト
         * @param parentIndices 親ジョイントのインデックスリスト
         * @return 計算されたSkeletonSignature
         */
        static SkeletonSignature calculateSignature(std::span<const std::string> hierarchyPaths, std::span<const std::int32_t> parentIndices) noexcept;
    };
}
