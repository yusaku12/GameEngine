#pragma once

#include "Core\Scene\Scene.h"

namespace Engine::Serialization
{
    /**
     * @brief SceneをFlatBuffersバイナリへ保存・復元するクラス。
     * @thread_safety Main thread only.
     */
    class SceneSerializer
    {
    public:
        /**
         * @brief SceneをFlatBuffersバイナリへ保存する。
         * @param path 保存先のファイルパス
         * @param scene 保存対象のScene
         * @return 保存に成功した場合はtrue
         */
        bool save(const std::filesystem::path& path, const Scene& scene) const;

        /**
         * @brief FlatBuffersバイナリからSceneを復元する。
         * @param path 読み込むファイルパス
         * @param scene 復元先のScene
         * @return 検証と復元に成功した場合はtrue
         */
        bool load(const std::filesystem::path& path, Scene& scene) const;
    };
} // namespace Engine::Serialization