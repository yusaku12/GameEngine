#pragma once

#include "Assets\Model\ModelTypes.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief CPU側ModelResourceのロード、共有、寿命を一元管理する。
     * @thread_safety Thread-safe.
     */
    class ModelManager
    {
    public:

        /**
         * @brief ModelManagerのSingletonを取得する。
         */
        static ModelManager& instance() noexcept;

        GE_DISABLE_COPY_AND_MOVE(ModelManager);

        /**
         * @brief FlatBuffersモデルまたはFBX SDK対応形式を読み込み、同じパスのResourceを共有する。
         * @param path 読み込むモデルアセット。.model/.mdlはSerializer、それ以外はFBX SDKを使用する
         * @return 成功時は有効なModelHandle
         */
        ModelHandle load(const std::filesystem::path& path);

        /**
         * @brief メモリ上のモデルを登録する。
         * @param model 登録するモデル
         * @param cacheKey 任意の重複防止Key。空の場合は常に新規登録する
         * @return 登録したModelHandle
         */
        ModelHandle create(ModelResource model, const std::filesystem::path& cacheKey = {});

        /**
         * @brief 現在のモデルSnapshotをFlatBuffersバイナリとして保存する。
         * @param handle 保存対象Handle
         * @param path 保存先のアセットパス
         * @return 保存に成功した場合はtrue
         */
        bool save(ModelHandle handle, const std::filesystem::path& path = {});

        /**
         * @brief Animation Clipをモデルの関連Animation一覧へ追加する。
         * @param model 対象Model Handle
         * @param clip 関連付けるAnimation Clip Handle
         * @return 関連付けに成功した場合はtrue
         */
        bool associateAnimationClip(ModelHandle model, AnimationClipHandle clip);

        /**
         * @brief MaterialをModel Slotのローカル既定値として設定する。
         * @param model 対象Model Handle
         * @param slotIndex 対象Material Slot
         * @param material 適用するMaterial snapshot
         * @return 登録したローカルMaterial Handle。失敗時は無効Handle
         */
        MaterialHandle setMaterialSlotMaterial(ModelHandle model, std::size_t slotIndex, MaterialAsset material);

        /**
         * @brief 外部ファイルのAnimationをModelに埋め込み、runtime Clipとして登録する。
         * @param model 対象Model Handle
         * @param path Animationを含むFBX等のファイルPath
         * @return 登録したClip Handle。失敗時は空
         */
        std::vector<AnimationClipHandle> importAnimationClips(ModelHandle model, const std::filesystem::path& path);

        /**
         * @brief Handleに対応するResourceの共有参照を取得する。
         * @param handle 取得するResourceのHandle
         */
        std::shared_ptr<const ModelResource> get(ModelHandle handle) const noexcept;

        /**
         * @brief Handleに対応するモデルアセットの保存Pathを取得する。
         * @param handle 取得するモデルHandle
         * @return 保存Path。メモリ上のモデルは空Pathを返す
         */
        std::filesystem::path getPath(ModelHandle handle) const;

        /**
         * @brief Handleに対応するResourceを管理対象から外す。
         * @param handle 管理対象から外すResourceのHandle
         */
        void unload(ModelHandle handle) noexcept;

        /**
         * @brief すべてのモデルとPath cacheを破棄する。
         */
        void clear() noexcept;

    private:

        /**
         * @brief モデルの管理情報。
         */
        struct Entry
        {
            std::shared_ptr<const ModelResource> resource; //!< モデルの共有リソース
            std::filesystem::path path;                    //!< モデルのパス。空の場合はPath cacheに登録しない
            std::uint32_t generation = 0;                  //!< Resourceの世代。Resourceが破棄されると世代が増える
        };

        ModelManager() = default;
        ~ModelManager() = default;

        /**
         * @brief パスを正規化する。
         * @param path 正規化するパス
         * @return 正規化されたパス
         */
        static std::filesystem::path normalizePath(const std::filesystem::path& path);

        mutable std::mutex m_mutex;                                         //!< スレッドセーフのためのMutex
        std::vector<Entry> m_entries;                                       //!< モデルの管理情報の配列
        std::unordered_map<std::filesystem::path, ModelHandle> m_pathCache; //!< パスからHandleへのキャッシュ
        std::uint32_t m_nextGeneration = 1;                                 //!< 次に割り当てる世代番号。0は無効世代なので1から開始
    };
} // namespace Engine