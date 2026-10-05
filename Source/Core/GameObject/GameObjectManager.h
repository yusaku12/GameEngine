#pragma once

#include "Core\CoreDefines.h"
#include "Core\GameObject\GameObject.h"

namespace Engine
{
    class AnimatorComponent;
    class ModelRendererComponent;

    /**
     * @brief GameObjectの生成・GUID検索・遅延破棄を管理するクラス。
     * @thread_safety Lifecycleと構造変更はGame update threadから直列に行うこと。
     * Animator評価、Transform階層のキャッシュ更新、ModelRenderer提出準備はJobSystem workerで並列実行し、
     * GameObjectManagerの構造は変更しない。
     */
    class GameObjectManager
    {
    public:

        GameObjectManager() = default;
        ~GameObjectManager() = default;

        GE_DISABLE_COPY_AND_MOVE(GameObjectManager);

        /**
         * @brief GameObjectを生成する。
         * @param name GameObjectの名前。省略時は"GameObject"。
         * @return 生成されたGameObjectのポインタ。生成できない場合はnullptr。
         */
        GameObject* create(const std::string& name = "GameObject");
        /** @brief GUIDを指定してGameObjectを生成する。生成できない場合はnullptrを返す。 */
        GameObject* create(const std::string& name, ObjectGUID guid);

        /**
         * @brief GameObjectを破棄する。
         * @details 破棄は遅延処理され、次のprocessDestroyQueue()呼び出し時に実行されます。
         * @param object 破棄するGameObjectのポインタ。
         * @return 破棄キューへの登録に成功した場合はtrue。
         */
        bool destroy(GameObject* object) noexcept;

        /**
         * @brief GameObjectを別のGameObjectManagerに移動する。
         * @details GameObjectとその子孫を直ちに移動します。容量確保に失敗した場合、状態を変更せずfalseを返します。
         * @param object 移動するGameObjectのポインタ。
         * @param destination 移動先のGameObjectManager。
         * @return 移動が成功した場合はtrue、失敗した場合はfalse。
         */
        bool transferTo(GameObject* object, GameObjectManager& destination) noexcept;

        /**
         * @brief 全GameObjectを別Managerの内容と置き換える。
         * @details sourceの所有・GUID索引が整合していない場合は、両Managerを変更せずfalseを返す。
         */
        bool replaceContentsFrom(GameObjectManager& source) noexcept;

        /**
         * @brief 破棄キューに溜まったGameObjectを処理する。
         * @details destroy()で遅延破棄されたGameObjectがここで実際に破棄されます。
         */
        void processDestroyQueue() noexcept;

        /**
         * @brief 全てのGameObjectを破棄する。
         */
        void clear() noexcept;

        /**
         * @brief 管理中GameObjectの有効なComponent Lifecycleを無効化する。
         * @details SceneがActiveでなくなる場合に呼び出し、再Active時は次回updateで再有効化する。
         */
        void deactivateLifecycle() noexcept;

        /**
         * @brief 全てのGameObjectを更新する。
         * @details Lifecycle対象を各phase開始時にsnapshotし、通常更新後にAnimatorの評価を並列実行して完了を待つ。
         * Lifecycle callbackで作成されたRootは、次のphase snapshotから対象になる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void update(float deltaTime) noexcept;

        /**
         * @brief 全てのGameObjectを固定時間間隔で更新する。
         * @param fixedDeltaTime 固定時間間隔（秒）。
         */
        void fixedUpdate(float fixedDeltaTime) noexcept;

        /**
         * @brief 全てのGameObjectを遅延更新する。
         * @details Transform階層のキャッシュ更新とLifecycle実行後、ModelRendererの描画Snapshot提出を並列実行して完了を待つ。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void lateUpdate(float deltaTime) noexcept;

        /**
         * @brief 名前でGameObjectを検索する。
         * @param name 検索するGameObjectの名前。
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* find(const std::string& name) noexcept;
        const GameObject* find(const std::string& name) const noexcept;

        /**
         * @brief GUIDでGameObjectを検索する。
         * @param guid 検索するGameObjectのGUID。
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* find(const ObjectGUID& guid) noexcept;
        const GameObject* find(const ObjectGUID& guid) const noexcept;

        /**
         * @brief タグでGameObjectを検索する。
         * @param tag 検索するGameObjectのタグ。
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* findWithTag(TagID tag) noexcept;

        /**
         * @brief タグでGameObjectを検索する。
         * @param tag 検索するGameObjectのタグ。
         * @return 見つかったGameObjectのポインタの配列。見つからなかった場合は空の配列。
         */
        std::vector<GameObject*> findGameObjectsWithTag(TagID tag) noexcept;

        /**
         * @brief レイヤーでGameObjectを検索する。
         * @param layer 検索するGameObjectのレイヤー。
         * @return 見つかったGameObjectのポインタの配列。見つからなかった場合は空の配列。
         */
        std::vector<GameObject*> findGameObjectsInLayer(LayerID layer) noexcept;

        /**
         * @brief 管理しているGameObjectの数を取得する。
         * @return GameObjectの数。
         */
        std::size_t size() const noexcept { return m_objects.size(); }

        /**
          * @brief Lifecycle callbackまたは破棄処理が実行中かどうかを取得する。
          * @return callbackまたはManager操作が実行中の場合はtrue。
          */
        bool isBusy() const noexcept
        {
            return m_isUpdatingLifecycle || m_isDeactivatingLifecycle
                || m_isProcessingDestroyQueue || m_isClearing;
        }

        /**
         * @brief 全てのGameObjectを取得する。
        * @return GameObjectのポインタの配列。
        */
        const auto& objects() const noexcept { return m_objects; }

    private:

        bool reserveUpdateBuffers(std::size_t capacity) noexcept;
        bool collectLifecycleRoots(std::vector<GameObject*>& roots) noexcept;

        std::vector<std::unique_ptr<GameObject>> m_objects;                      //!< 管理しているGameObjectの配列
        std::vector<GameObject*> m_destroyQueue;                                 //!< 破棄キューに溜まったGameObjectの配列
        std::unordered_map<ObjectGUID, GameObject*, ObjectGUIDHash> m_guidIndex; //!< GUIDでGameObjectを検索するためのインデックス
        std::vector<GameObject*> m_lifecycleRoots;                               //!< Lifecycle phase用のRoot snapshot
        std::vector<GameObject*> m_deactivationRoots;                            //!< Lifecycle無効化用のRoot snapshot
        std::vector<AnimatorComponent*> m_animatorsToUpdate;                     //!< 並列評価するAnimatorの一時一覧。所有権は持たない。
        std::vector<ModelRendererComponent*> m_modelRenderersToSubmit;           //!< 並列提出するModelRenderer一覧。所有権は持たない。
        std::vector<const GameObject*> m_transformRootsToUpdate;                 //!< 並列更新するTransform階層のルート。所有権は持たない。
        bool m_isUpdatingLifecycle = false;                                      //!< Lifecycle callbackからの再入を防ぐ
        bool m_isDeactivatingLifecycle = false;                                  //!< Lifecycle無効化 callbackからの再入を防ぐ
        bool m_isProcessingDestroyQueue = false;                                 //!< Destroy callback中の破棄処理再入を防ぐ
        bool m_isClearing = false;                                               //!< GameObject全消去中かどうか
    };
} // namespace Engine