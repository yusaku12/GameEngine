#pragma once

#include "Assets\Animation\SkeletonAsset.h"

namespace Engine::Serialization
{
    /**
     * @brief SkeletonAssetのシリアライズを行うクラス
     */
    class SkeletonSerializer
    {
    public:

        /**
         * @brief SkeletonAssetを指定パスへ保存する
         * @param path 保存先のファイルパス
         * @param skeleton 保存するSkeletonAsset
         * @return 保存に成功した場合はtrue
         */
        bool save(const std::filesystem::path& path, const SkeletonAsset& skeleton) const;

        /**
         * @brief 指定パスからSkeletonAssetを読み込む
         * @param path 読み込むファイルパス
         * @param skeleton 読み込んだSkeletonAssetの出力先
         * @return 読み込みに成功した場合はtrue
         */
        bool load(const std::filesystem::path& path, SkeletonAsset& skeleton) const;
    };
}
