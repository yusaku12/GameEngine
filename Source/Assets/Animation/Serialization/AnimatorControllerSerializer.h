#pragma once

#include "Assets\Animation\AnimatorControllerAsset.h"

namespace Engine::Serialization
{
    /**
     * @brief AnimatorControllerAssetのFlatBuffersシリアライズを行う。
     */
    class AnimatorControllerSerializer
    {
    public:

        /**
         * @brief Controllerを指定パスへ保存する。
         * @param path 保存先のファイルパス。
         * @param controller 保存するAnimatorControllerAsset。
         * @return 保存に成功した場合はtrue、それ以外はfalse。
         */
        bool save(const std::filesystem::path& path, const AnimatorControllerAsset& controller) const;

        /**
         * @brief 指定パスからControllerを読み込む。
         * @param path 読み込むファイルパス。
         * @param controller 読み込んだAnimatorControllerAssetを格納する変数への参照。
         * @return 読み込みに成功した場合はtrue、それ以外はfalse。
         */
        bool load(const std::filesystem::path& path, AnimatorControllerAsset& controller) const;
    };
}
