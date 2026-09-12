#pragma once

#include "Core\GameObject\GameObjectManager.h"

namespace Engine
{
    /**
     * @brief GameObjectの所有単位となるScene。
     * @thread_safety Main thread only.
     */
    class Scene
    {
    public:

        /**
         * @brief GUIDと名前を指定してSceneを生成する。
         * @param guid SceneのGUID
         * @param name Scene名
         */
        Scene(ObjectGUID guid, std::string name);
        ~Scene() = default;

        GE_DISABLE_COPY_AND_MOVE(Scene);

        /**
         * @brief SceneのGUIDを取得する。
         */
        const ObjectGUID& getGUID() const noexcept { return m_guid; }

        /**
         * @brief Scene名を取得する。
         * @return Scene名
         */
        const std::string& getName() const noexcept { return m_name; }

        /**
         * @brief Scene名を設定する。
         * @param name 設定するScene名
         */
        void setName(std::string name) { m_name = std::move(name); }

        /**
         * @brief SceneのGUIDを設定する。
         * @param guid 設定するSceneのGUID
         */
        void setGUID(ObjectGUID guid) noexcept { m_guid = guid; }

        /**
         * @brief SceneにGameObjectを生成する。
         * @param name 生成するGameObjectの名前
         * @return 生成されたGameObjectのポインタ
         */
        GameObject* createGameObject(const std::string& name = "GameObject")
        {
            return m_objects.create(name);
        }

        /**
         * @brief GUIDを指定してSceneにGameObjectを生成する。
         * @param name 生成するGameObjectの名前
         * @param guid 生成するGameObjectのGUID
         * @return 生成されたGameObjectのポインタ
         */
        GameObject* createGameObject(const std::string& name, ObjectGUID guid)
        {
            return m_objects.create(name, guid);
        }

        /**
         * @brief GameObjectを破棄キューへ追加する。
         * @param object 破棄するGameObjectのポインタ
         */
        void destroyGameObject(GameObject* object) noexcept { m_objects.destroy(object); }

        /**
         * @brief GameObjectを別Sceneへ移動する。
         * @param object 移動するGameObjectのポインタ
         * @param destination 移動先のScene
         * @return 移動に成功した場合はtrue、存在しない場合はfalse
         */
        bool moveGameObjectTo(GameObject* object, Scene& destination) noexcept
        {
            return m_objects.transferTo(object, destination.m_objects);
        }

        /**
         * @brief 通常更新を実行する。
         * @param deltaTime 経過時間
         */
        void update(float deltaTime) noexcept { m_objects.update(deltaTime); }

        /**
         * @brief 固定時間刻みの更新を実行する。
         * @param fixedDeltaTime 固定時間刻みの経過時間
         */
        void fixedUpdate(float fixedDeltaTime) noexcept { m_objects.fixedUpdate(fixedDeltaTime); }

        /**
         * @brief 遅延更新を実行する。
         * @param deltaTime 経過時間
         */
        void lateUpdate(float deltaTime) noexcept { m_objects.lateUpdate(deltaTime); }

        /**
         * @brief 破棄キューを処理する。
         */
        void processDestroyQueue() noexcept { m_objects.processDestroyQueue(); }

        /**
         * @brief 名前からGameObjectを検索する。
         * @param name 検索するGameObjectの名前
         * @return 見つかったGameObjectのポインタ、存在しない場合はnullptr
         */
        GameObject* find(const std::string& name) noexcept { return m_objects.find(name); }
        const GameObject* find(const std::string& name) const noexcept { return m_objects.find(name); }

        /**
         * @brief GUIDからGameObjectを検索する。
         * @param guid 検索するGameObjectのGUID
         * @return 見つかったGameObjectのポインタ、存在しない場合はnullptr
         */
        GameObject* find(const ObjectGUID& guid) noexcept { return m_objects.find(guid); }
        const GameObject* find(const ObjectGUID& guid) const noexcept { return m_objects.find(guid); }

        /**
         * @brief Tagを持つ最初のGameObjectを検索する。
         * @param tag 検索するTagID
         * @return 見つかったGameObjectのポインタ、存在しない場合はnullptr
         */
        GameObject* findWithTag(TagID tag) noexcept { return m_objects.findWithTag(tag); }

        /**
         * @brief Tagを持つGameObjectをすべて検索する。
         * @param tag 検索するTagID
         * @return 見つかったGameObjectのポインタのベクター、存在しない場合は空のベクター
         */
        std::vector<GameObject*> findGameObjectsWithTag(TagID tag) noexcept { return m_objects.findGameObjectsWithTag(tag); }

        /**
         * @brief Layerに属するGameObjectをすべて検索する。
         * @param layer 検索するLayerID
         * @return 見つかったGameObjectのポインタのベクター、存在しない場合は空のベクター
         */
        std::vector<GameObject*> findGameObjectsInLayer(LayerID layer) noexcept { return m_objects.findGameObjectsInLayer(layer); }

        /**
         * @brief Scene内のGameObject数を取得する。
         * @return Scene内のGameObject数
         */
        std::size_t getGameObjectCount() const noexcept { return m_objects.size(); }

        /**
         * @brief Scene内のGameObject一覧を取得する。
         * @return Scene内のGameObjectのポインタのベクター
         */
        const auto& getGameObjects() const noexcept { return m_objects.objects(); }

        /**
         * @brief Scene内のGameObjectをすべて破棄する。
         */
        void clear() noexcept { m_objects.clear(); }

    private:

        ObjectGUID m_guid;           //!< SceneのGUID
        std::string m_name;          //!< Scene名
        GameObjectManager m_objects; //!< 所有するGameObjectの管理
    };
} // namespace Engine