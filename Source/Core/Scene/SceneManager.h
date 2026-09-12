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
         * @brief 名前を指定したSceneを管理対象から削除する。
         * @param name 削除するSceneの名前
         * @return 削除に成功した場合はtrue、存在しない場合はfalse
         */
        bool removeScene(const std::string& name) noexcept;

        std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes; //!< 名前からSceneへの索引
        Scene m_persistentScene;                                          //!< Scene切り替え後も保持する永続Scene
        Scene* m_activeScene = nullptr;                                   //!< 現在アクティブなScene。所有しない
    };
} // namespace Engine