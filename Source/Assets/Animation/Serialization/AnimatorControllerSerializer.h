#pragma once

#include "Assets\Animation\AnimatorControllerAsset.h"

namespace Engine::Serialization
{
    /** @brief AnimatorControllerAssetのFlatBuffersシリアライズを行う。 */
    class AnimatorControllerSerializer
    {
    public:
        /** @brief Controllerを指定パスへ保存する。 */
        bool save(const std::filesystem::path& path, const AnimatorControllerAsset& controller) const;

        /** @brief 指定パスからControllerを読み込む。 */
        bool load(const std::filesystem::path& path, AnimatorControllerAsset& controller) const;
    };
}
