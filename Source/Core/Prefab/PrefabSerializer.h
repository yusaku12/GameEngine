#pragma once

#include "Core\Prefab\Prefab.h"

namespace Engine::Serialization
{
    /**
     * @brief PrefabをFlatBuffersバイナリへ保存・復元するクラス。
     * @thread_safety Main thread only.
     */
    class PrefabSerializer
    {
    public:

        /**
         * @brief PrefabをFlatBuffersバイナリへ保存する。
         * @param path 保存先のファイルパス
         * @param prefab 保存対象のPrefab
         * @return 保存に成功した場合はtrue
         */
        bool save(const std::filesystem::path& path, const Prefab& prefab) const;

        /**
         * @brief FlatBuffersバイナリからPrefabを復元する。
         * @param path 読み込むファイルパス
         * @param prefab 復元先のPrefab
         * @return 検証と復元に成功した場合はtrue
         */
        bool load(const std::filesystem::path& path, Prefab& prefab) const;
    };
} // namespace Engine::Serialization