#pragma once

#include "Assets\Model\Resource\ModelResource.h"

namespace Engine::Serialization
{
    /**
     * @brief モデルのシリアライズを行うクラス。
     */
    class ModelSerializer
    {
    public:

        /**
         * @brief モデルをFlatBuffersバイナリとして保存する。
         * @param path 保存先のアセットパス。
         * @param model 保存するCPU側モデル。
         * @return 保存に成功した場合はtrue。
         */
        bool save(const std::filesystem::path& path, const ModelResource& model) const;

        /**
         * @brief FlatBuffersバイナリからモデルを読み込む。
         * @param path 読み込むアセットパス。
         * @param model 読み込み先のCPU側モデル。
         * @return 検証と読み込みに成功した場合はtrue。
         */
        bool load(const std::filesystem::path& path, ModelResource& model) const;
    };
}