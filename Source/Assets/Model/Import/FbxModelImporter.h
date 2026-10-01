#pragma once

#include "Assets\Model\Resource\ModelResource.h"

namespace Engine
{
    /**
     * @brief FBX SDKを使用して外部シーンをCPU側ModelResourceへ変換するImporter。
     * @thread_safety Thread-safe. Each import owns its FBX manager and scene.
     */
    class FbxModelImporter
    {
    public:

        /**
         * @brief モデルファイルをインポートする。
         * @param path 読み込むモデルファイルのパス
         * @return 成功時はModelResource、失敗時はnullptr
         */
        std::shared_ptr<ModelResource> importModel(const std::filesystem::path& path) const;

        /**
         * @brief 外部ファイルのアニメーションを秒単位のキーへ変換する。
         * @param path アニメーションを含むFBXファイルのパス
         * @return 読み込んだアニメーション。失敗時は空の配列
         */
        std::vector<AnimationResource> importAnimations(const std::filesystem::path& path) const;
    };
} // namespace Engine