#pragma once

#include "Assets\Animation\AnimationClipAsset.h"

namespace Engine::Serialization
{
    /**
     * @brief AnimationClipAsset のシリアライズを行うクラス
     */
    class AnimationClipSerializer
    {
    public:

        /**
         * @brief AnimationClipAsset を指定パスへ保存する
         * @param path 保存先のパス
         * @param clip 保存する AnimationClipAsset
         * @return 保存に成功した場合は true
         */
        bool save(const std::filesystem::path& path, const AnimationClipAsset& clip) const;

        /**
         * @brief 指定パスから AnimationClipAsset を読み込む
         * @param path 読み込み元のパス
         * @param clip 読み込んだ AnimationClipAsset の出力先
         * @return 読み込みに成功した場合は true
         */
        bool load(const std::filesystem::path& path, AnimationClipAsset& clip) const;
    };
}
