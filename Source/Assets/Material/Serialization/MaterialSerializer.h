#pragma once

#include <filesystem>
#include "Assets\Material\MaterialAsset.h"

namespace Engine::Serialization
{
    /**
     * @brief Material AssetのFlatBuffers入出力を行う。
     */
    class MaterialSerializer
    {
    public:

        /**
         * @brief Material AssetをAtomic Saveする。Texture Hintは保存先ファイル基準の相対パス。
         * @param path 保存先のアセットパス
         * @param material 保存するMaterial Asset。Texture Hint入力は絶対パスまたは作業ディレクトリ基準。
         * @return 保存に成功した場合はtrue
         */
        bool save(const std::filesystem::path& path, const MaterialAsset& material) const;

        /**
         * @brief Material Assetを検証して読み込む。
         * @param path 読み込むアセットパス
         * @param material 読み込み先。Texture Hintは絶対パスへ解決される。
         * @return 検証と読み込みに成功した場合はtrue
         */
        bool load(const std::filesystem::path& path, MaterialAsset& material) const;
    };
} // namespace Engine::Serialization