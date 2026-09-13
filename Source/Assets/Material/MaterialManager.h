#pragma once

#include "Assets\Material\MaterialAsset.h"
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief Material Assetのロード、共有、寿命を一元管理する。
     * @thread_safety Thread-safe.
     */
    class MaterialManager
    {
    public:

        /**
         * @brief MaterialManagerのSingletonを取得する。
         */
        static MaterialManager& instance() noexcept;

        GE_DISABLE_COPY_AND_MOVE(MaterialManager);

        /**
         * @brief FlatBuffers Material Assetを読み込む。
         * @param path 読み込むアセットパス
         */
        MaterialHandle load(const std::filesystem::path& path);

        /**
         * @brief メモリ上のMaterial Assetを登録する。無効GUIDには新しいGUIDを割り当てる。
         * @param material 登録するMaterial Asset
         * @param cacheKey キャッシュキー
         */
        MaterialHandle create(MaterialAsset material, const std::filesystem::path& cacheKey = {});

        /**
         * @brief Materialを新しい不変Snapshotへ置換する。
         * @param handle 更新対象Handle
         * @param material 新しいMaterial値。GUIDは既存値を維持する
         * @return 更新に成功した場合はtrue
         */
        bool update(MaterialHandle handle, MaterialAsset material);

        /**
         * @brief 現在のMaterial SnapshotをAtomic Saveする。
         * @param handle 保存対象Handle
         * @param path 保存先。空の場合は既存Asset Pathを使用する
         * @return 保存に成功した場合はtrue
         */
        bool save(MaterialHandle handle, const std::filesystem::path& path = {});

        /**
         * @brief Handleに対応する不変Material Assetを取得する。
         * @param handle 取得するMaterial Handle
         * @return 取得したMaterial Assetの共有ポインタ
         */
        std::shared_ptr<const MaterialAsset> get(MaterialHandle handle) const noexcept;

        /**
         * @brief GUIDに対応するMaterial Handleを取得する。
         * @param guid 取得するMaterialのGUID
         * @return 取得したMaterial Handle
         */
        MaterialHandle findByGuid(const AssetGUID& guid) const noexcept;

        /**
         * @brief 現在ロード済みのMaterial Handle Snapshotを取得する。
         * @return 取得したMaterial Handleのベクター
         */
        std::vector<MaterialHandle> getAllHandles() const;

        /**
         * @brief Material Assetの保存Pathを取得する。メモリ上のMaterialは空Pathを返す。
         * @param handle 取得するMaterial Handle
         */
        std::filesystem::path getPath(MaterialHandle handle) const;

        /**
         * @brief Handleに対応するMaterial Assetを管理対象から外す。組み込みMaterialは保持する。
         * @param handle 解除するMaterial Handle
         */
        void unload(MaterialHandle handle) noexcept;

        /**
         * @brief 組み込みMaterial以外を破棄する。
         */
        void clear() noexcept;

        /**
         * @brief Engine組み込みDefault Materialを取得する。
         * @return 取得したDefault Material Handle
         */
        MaterialHandle getDefaultMaterial() const noexcept;

        /**
         * @brief Engine組み込みError Materialを取得する。
         * @return 取得したError Material Handle
         */
        MaterialHandle getErrorMaterial() const noexcept;

    private:

        /**
         * @brief Material Assetの管理情報。
         */
        struct Entry
        {
            std::shared_ptr<const MaterialAsset> resource; //!< Material Assetの共有ポインタ
            std::filesystem::path path;                    //!< Material Assetのアセットパス
            std::uint32_t generation = 0;                  //!< 世代番号
            bool builtIn = false;                          //!< 組み込みMaterialかどうか
        };

        MaterialManager();
        ~MaterialManager() = default;

        /**
         * @brief パスを正規化する。
         * @param path 正規化するパス
         * @return 正規化されたパス
         */
        static std::filesystem::path normalizePath(const std::filesystem::path& path);

        /**
         * @brief Material Assetを管理対象に追加する。
         * @param material 追加するMaterial Asset
         * @param path 追加するMaterial Assetのアセットパス
         * @param builtIn 組み込みMaterialかどうか
         * @return 追加したMaterial Handle
         */
        MaterialHandle addEntry(MaterialAsset material, const std::filesystem::path& path, bool builtIn);

        /**
         * @brief 組み込みMaterialを初期化する。
         */
        void initializeBuiltIns();

        mutable std::mutex m_mutex;                                                //!< MaterialManagerのスレッドセーフ用ミューテックス
        std::vector<Entry> m_entries;                                              //!< Material Assetの管理情報のベクター
        std::unordered_map<std::filesystem::path, MaterialHandle> m_pathCache;     //!< Material AssetのアセットパスからMaterial Handleへのキャッシュ
        std::unordered_map<AssetGUID, MaterialHandle, ObjectGUIDHash> m_guidCache; //!< Material AssetのGUIDからMaterial Handleへのキャッシュ
        std::uint32_t m_nextGeneration = 1;                                        //!< 次の世代番号
        MaterialHandle m_defaultMaterial;                                          //!< Engine組み込みDefault Material Handle
        MaterialHandle m_errorMaterial;                                            //!< Engine組み込みError Material Handle
    };
} // namespace Engine