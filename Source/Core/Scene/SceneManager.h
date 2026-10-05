#pragma once

#include "Core\CoreDefines.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    /**
     * @brief Sceneの生成・切り替え・破棄を管理するクラス。
     * @thread_safety Main thread only.
     */
    class SceneManager
    {
    public:

        /**
         * @brief SceneManagerのインスタンスを取得する。
         */
        static SceneManager& instance()
        {
            static SceneManager instance;
            return instance;
        }

        /**
         * @brief 永続Sceneを初期化してSceneManagerを生成する。
         */
        SceneManager();
        ~SceneManager() = default;

        GE_DISABLE_COPY_AND_MOVE(SceneManager);

        /**
         * @brief 管理中の全SceneとGameObjectを破棄する。
         *
         * @details ComponentのLifecycleをSingleton Managerの終了前に完了させる。
         */
        void shutdown() noexcept;

        /**
         * @brief 名前を指定してSceneを生成する。
         * @param name 生成するSceneの名前
         * @return 生成されたSceneのポインタ
         */
        Scene* createScene(const std::string& name);

        /**
         * @brief 名前を指定したSceneを破棄する。
         * @param name 破棄するSceneの名前
         * @return 破棄に成功した場合はtrue、存在しない場合はfalse
         */
        bool destroyScene(const std::string& name) noexcept;

        /**
         * @brief 名前を指定したSceneをロードする。
         * @param name ロードするSceneの名前
         * @return ロードに成功した場合はtrue、存在しない場合はfalse
         */
        bool loadScene(const std::string& name) noexcept;

        /**
         * @brief 名前を指定したSceneをアンロードする。
         * @param name アンロードするSceneの名前
         * @return アンロードに成功した場合はtrue、存在しない場合はfalse
         */
        bool unloadScene(const std::string& name) noexcept;

        /**
         * @brief アクティブSceneを設定する。
         * @param scene 設定するSceneのポインタ
         * @return 設定に成功した場合はtrue、存在しない場合はfalse
         */
        bool setActiveScene(Scene* scene) noexcept;

        /**
         * @brief GameObjectを永続Sceneへ移動する。
         * @param object 移動するGameObjectのポインタ
         * @return 移動に成功した場合はtrue、存在しない場合はfalse
         */
        bool dontDestroyOnLoad(GameObject* object) noexcept;

        /**
         * @brief 現在アクティブなSceneを取得する。
         * @return 現在アクティブなSceneのポインタ
         */
        Scene* getActiveScene() noexcept { return m_activeScene; }
        const Scene* getActiveScene() const noexcept { return m_activeScene; }

        /**
         * @brief ロード間で保持される永続Sceneを取得する。
         * @return 永続Sceneのポインタ
         */
        Scene* getPersistentScene() noexcept { return &m_persistentScene; }

        /**
         * @brief 名前からSceneを検索する。.
         * @param name 検索するSceneの名前
         * @return 見つかったSceneのポインタ、存在しない場合はnullptr
         */
        Scene* findScene(const std::string& name) noexcept;
        const Scene* findScene(const std::string& name) const noexcept;

        /**
         * @brief アクティブSceneを通常更新する。
         * @param deltaTime 経過時間
         */
        void update(float deltaTime) noexcept;

        /**
         * @brief アクティブSceneを固定時間刻みで更新する。
         * @param fixedDeltaTime 固定時間刻みの経過時間
         */
        void fixedUpdate(float fixedDeltaTime) noexcept;

        /**
         * @brief アクティブSceneを遅延更新する。
         * @param deltaTime 経過時間
         */
        void lateUpdate(float deltaTime) noexcept;

        /**
         * @brief 破棄待ちGameObjectを処理する。
         * @return void
         */
        void processDestroyQueue() noexcept;

    private:

        /**
         * @brief Scene操作の種類を表す列挙型。
         */
        enum class PendingOperationKind
        {
            SetActive, //!< アクティブSceneの設定
            Remove     //!< Sceneの削除
        };

        /**
         * @brief 保留中のScene操作を表す構造体。
         */
        struct PendingOperation
        {
            PendingOperationKind kind; //!< 操作の種類
            Scene* scene;              //!< 操作対象のScene
        };

        /**
         * @brief 名前を指定したSceneを管理対象から削除する。
         * @param name 削除するSceneの名前
         * @return 削除に成功した場合はtrue、存在しない場合はfalse
         */
        bool removeScene(const std::string& name) noexcept;
        bool removeScene(Scene* scene) noexcept;
        bool removeSceneNow(Scene* scene) noexcept;
        bool setActiveSceneNow(Scene* scene) noexcept;
        bool queueOperation(PendingOperationKind kind, Scene* scene) noexcept;
        void applyPendingOperations() noexcept;

        std::vector<std::unique_ptr<Scene>> m_scenes;      //!< 所有するScene
        Scene m_persistentScene;                           //!< Scene切り替え後も保持する永続Scene
        Scene* m_activeScene = nullptr;                    //!< 現在アクティブなScene。所有しない
        std::vector<PendingOperation> m_pendingOperations; //!< Lifecycle callback後に適用するScene操作
        Scene* m_sceneBeingRemoved = nullptr;              //!< Lifecycle終了中のScene
        bool m_isDispatching = false;                      //!< Scene/Component callback実行中か
        bool m_isApplyingOperations = false;               //!< 保留操作の適用中か
        bool m_isShuttingDown = false;                     //!< shutdown処理中か
    };
} // namespace Engine