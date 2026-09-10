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

        /** @brief SceneManagerのインスタンスを取得する。 */
        static SceneManager& instance()
        {
            static SceneManager instance;
            return instance;
        }

        /** @brief 永続Sceneを初期化してSceneManagerを生成する。 */
        SceneManager();
        ~SceneManager() = default;

        GE_DISABLE_COPY_AND_MOVE(SceneManager);

        /** @brief 名前を指定してSceneを生成する。 */
        Scene* createScene(const std::string& name);

        /** @brief 名前を指定したSceneを破棄する。 */
        bool destroyScene(const std::string& name) noexcept;

        /** @brief 名前を指定したSceneをロードする。 */
        bool loadScene(const std::string& name) noexcept;

        /** @brief 名前を指定したSceneをアンロードする。 */
        bool unloadScene(const std::string& name) noexcept;

        /** @brief アクティブSceneを設定する。 */
        bool setActiveScene(Scene* scene) noexcept;

        /** @brief GameObjectを永続Sceneへ移動する。 */
        bool dontDestroyOnLoad(GameObject* object) noexcept;

        /** @brief 現在アクティブなSceneを取得する。 */
        Scene* getActiveScene() noexcept { return m_activeScene; }
        /** @brief 現在アクティブなSceneをconstで取得する。 */
        const Scene* getActiveScene() const noexcept { return m_activeScene; }

        /** @brief ロード間で保持される永続Sceneを取得する。 */
        Scene* getPersistentScene() noexcept { return &m_persistentScene; }

        /** @brief 名前からSceneを検索する。 */
        Scene* findScene(const std::string& name) noexcept;
        /** @brief 名前からSceneをconstで検索する。 */
        const Scene* findScene(const std::string& name) const noexcept;

        /** @brief アクティブSceneを通常更新する。 */
        void update(float deltaTime) noexcept;

        /** @brief アクティブSceneを固定時間刻みで更新する。 */
        void fixedUpdate(float fixedDeltaTime) noexcept;

        /** @brief アクティブSceneを遅延更新する。 */
        void lateUpdate(float deltaTime) noexcept;

        /** @brief 破棄待ちGameObjectを処理する。 */
        void processDestroyQueue() noexcept;

    private:

        /** @brief 名前を指定したSceneを管理対象から削除する。 */
        bool removeScene(const std::string& name) noexcept;

        std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes; //!< 名前からSceneへの索引
        Scene m_persistentScene; //!< Scene切り替え後も保持する永続Scene
        Scene* m_activeScene = nullptr; //!< 現在アクティブなScene。所有しない
    };
} // namespace Engine