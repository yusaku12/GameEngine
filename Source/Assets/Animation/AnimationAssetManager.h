#pragma once

#include "Assets\Animation\AnimationClipAsset.h"
#include "Assets\Animation\AnimatorControllerAsset.h"
#include "Assets\Animation\SkeletonAsset.h"
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief Skeleton と AnimationClip のロード、キャッシュ、アンロードを管理するシングルトン。
     * @thread_safety Thread-safe.
     */
    class AnimationAssetManager
    {
    public:

        static AnimationAssetManager& instance() noexcept;
        GE_DISABLE_COPY_AND_MOVE(AnimationAssetManager);

        /**
         * @brief 指定パスの Skeleton をロードする。既にロード済みの場合はキャッシュを返す。
         * @param path Skeleton アセットのパス
         * @return SkeletonHandle
         */
        SkeletonHandle loadSkeleton(const std::filesystem::path& path);

        /**
         * @brief SkeletonAsset から Skeleton を作成する。既に同じ GUID の Skeleton が存在する場合はキャッシュを返す。
         * @param asset SkeletonAsset
         * @param cacheKey キャッシュキー (省略時は GUID を使用)
         * @return SkeletonHandle
         */
        SkeletonHandle createSkeleton(SkeletonAsset asset, const std::filesystem::path& cacheKey = {});

        /**
         * @brief SkeletonHandle に対応する SkeletonAsset を指定パスに保存する。
         * @param handle SkeletonHandle
         * @param path 保存先のパス
         * @return 保存に成功した場合は true
         */
        bool saveSkeleton(SkeletonHandle handle, const std::filesystem::path& path) const;

        /**
         * @brief SkeletonHandle に対応する SkeletonAsset を取得する。
         * @param handle SkeletonHandle
         * @return SkeletonAsset への shared_ptr。無効な handle の場合は nullptr。
         */
        std::shared_ptr<const SkeletonAsset> getSkeleton(SkeletonHandle handle) const noexcept;

        /**
         * @brief Skeleton の GUID から SkeletonHandle を検索する。
         * @param guid Skeleton の GUID
         * @return SkeletonHandle。見つからない場合は無効な handle。
         */
        SkeletonHandle findSkeletonByGuid(const AssetGUID& guid) const noexcept;

        /**
         * @brief SkeletonHandle に対応する Skeleton をアンロードする。
         * @param handle SkeletonHandle
         */
        void unloadSkeleton(SkeletonHandle handle) noexcept;

        /**
         * @brief 指定パスの AnimationClip をロードする。既にロード済みの場合はキャッシュを返す。
         * @param path AnimationClip アセットのパス
         * @return AnimationClipHandle
         */
        AnimationClipHandle loadClip(const std::filesystem::path& path);

        /**
         * @brief AnimationClipAsset から AnimationClip を作成する。既に同じ GUID の AnimationClip が存在する場合はキャッシュを返す。
         * @param asset AnimationClipAsset
         * @param cacheKey キャッシュキー (省略時は GUID を使用)
         * @return AnimationClipHandle
         */
        AnimationClipHandle createClip(AnimationClipAsset asset, const std::filesystem::path& cacheKey = {});

        /**
         * @brief AnimationClipHandle に対応する AnimationClipAsset を指定パスに保存する。
         * @param handle AnimationClipHandle
         * @param path 保存先のパス
         * @return 保存に成功した場合は true
         */
        bool saveClip(AnimationClipHandle handle, const std::filesystem::path& path) const;

        /**
         * @brief AnimationClipHandle に対応する AnimationClipAsset を取得する。
         * @param handle AnimationClipHandle
         * @return AnimationClipAsset への shared_ptr。無効な handle の場合は nullptr。
         */
        std::shared_ptr<const AnimationClipAsset> getClip(AnimationClipHandle handle) const noexcept;

        /**
         * @brief AnimationClip の GUID から AnimationClipHandle を検索する。
         * @param guid AnimationClip の GUID
         * @return AnimationClipHandle。見つからない場合は無効な handle。
         */
        AnimationClipHandle findClipByGuid(const AssetGUID& guid) const noexcept;

        /**
         * @brief AnimationClipHandle に対応する AnimationClip をアンロードする。
         * @param handle AnimationClipHandle
         */
        void unloadClip(AnimationClipHandle handle) noexcept;

        /**
         * @brief 指定パスのAnimator Controllerをロードする。
         * @param path Animator Controllerのパス
         */
        AnimatorControllerHandle loadController(const std::filesystem::path& path);

        /**
         * @brief Controller assetをimmutable cacheへ登録する。
         * @param asset Controller asset
         * @param cacheKey キャッシュキー (省略時は GUID を使用)
         * @return AnimatorControllerHandle
         */
        AnimatorControllerHandle createController(AnimatorControllerAsset asset, const std::filesystem::path& cacheKey = {});

        /**
         * @brief Controller assetを指定パスへ保存する。
         * @param handle AnimatorControllerHandle
         * @param path 保存先のパス
         * @return 保存に成功した場合は true
         */
        bool saveController(AnimatorControllerHandle handle, const std::filesystem::path& path) const;

        /**
         * @brief 世代が一致するController snapshotを取得する。
         * @param handle AnimatorControllerHandle
         * @return AnimatorControllerAsset への shared_ptr。無効な handle の場合は nullptr。
         */
        std::shared_ptr<const AnimatorControllerAsset> getController(AnimatorControllerHandle handle) const noexcept;

        /**
         * @brief GUIDからController handleを検索する。
         * @param guid AnimatorController の GUID
         * @return AnimatorControllerHandle。見つからない場合は無効な handle。
         */
        AnimatorControllerHandle findControllerByGuid(const AssetGUID& guid) const noexcept;

        /**
         * @brief Controllerをcacheから除外する。
         * @param handle AnimatorControllerHandle
         */
        void unloadController(AnimatorControllerHandle handle) noexcept;

        /**
         * @brief Skeleton と AnimationClip のキャッシュをすべてクリアする。
         */
        void clear() noexcept;

    private:

        /**
         * @brief Skeleton と AnimationClip のキャッシュエントリ。
         * @tparam Asset SkeletonAsset または AnimationClipAsset
         */
        template<class Asset>
        struct Entry
        {
            std::shared_ptr<const Asset> resource; //!< キャッシュされた SkeletonAsset または AnimationClipAsset への shared_ptr
            std::filesystem::path path;            //!< キャッシュされたアセットのパス
            std::uint32_t generation = 0;          //!< キャッシュの世代番号。アンロード時にインクリメントされる。
        };

        AnimationAssetManager() = default;
        ~AnimationAssetManager() = default;

        /**
         * @brief パスを正規化する。大文字小文字の違いを無視するために小文字に変換する。
         * @param path 正規化するパス
         * @return 正規化されたパス
         */
        static std::filesystem::path normalizePath(const std::filesystem::path& path);

        mutable std::mutex m_mutex;                                                                //!< キャッシュの保護用 Mutex
        std::vector<Entry<SkeletonAsset>> m_skeletons;                                             //!< キャッシュされた SkeletonAsset のリスト
        std::vector<Entry<AnimationClipAsset>> m_clips;                                            //!< キャッシュされた AnimationClipAsset のリスト
        std::vector<Entry<AnimatorControllerAsset>> m_controllers;                                 //!< キャッシュされた AnimatorControllerAsset のリスト
        std::unordered_map<std::filesystem::path, SkeletonHandle> m_skeletonPaths;                 //!< パスから SkeletonHandle へのマッピング
        std::unordered_map<std::filesystem::path, AnimationClipHandle> m_clipPaths;                //!< パスから AnimationClipHandle へのマッピング
        std::unordered_map<std::filesystem::path, AnimatorControllerHandle> m_controllerPaths;     //!< パスから AnimatorControllerHandle へのマッピング
        std::unordered_map<AssetGUID, SkeletonHandle, ObjectGUIDHash> m_skeletonGuids;             //!< GUID から SkeletonHandle へのマッピング
        std::unordered_map<AssetGUID, AnimationClipHandle, ObjectGUIDHash> m_clipGuids;            //!< GUID から AnimationClipHandle へのマッピング
        std::unordered_map<AssetGUID, AnimatorControllerHandle, ObjectGUIDHash> m_controllerGuids; //!< GUID から AnimatorControllerHandle へのマッピング
        std::uint32_t m_nextSkeletonGeneration = 1;                                                //!< SkeletonHandle の世代番号の次の値
        std::uint32_t m_nextClipGeneration = 1;                                                    //!< AnimationClipHandle の世代番号の次の値
        std::uint32_t m_nextControllerGeneration = 1;                                              //!< AnimatorControllerHandle の世代番号の次の値
    };
}
