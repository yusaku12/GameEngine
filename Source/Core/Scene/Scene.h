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

        /** @brief SceneのGUIDを取得する。 */
        const ObjectGUID& getGUID() const noexcept { return m_guid; }

        /** @brief Scene名を取得する。 */
        const std::string& getName() const noexcept { return m_name; }

        /** @brief Scene名を設定する。 */
        void setName(std::string name) { m_name = std::move(name); }

        /** @brief SceneのGUIDを設定する。 */
        void setGUID(ObjectGUID guid) noexcept { m_guid = guid; }

        /** @brief SceneにGameObjectを生成する。 */
        GameObject* createGameObject(const std::string& name = "GameObject")
        {
            return m_objects.create(name);
        }

        /** @brief GUIDを指定してSceneにGameObjectを生成する。 */
        GameObject* createGameObject(const std::string& name, ObjectGUID guid)
        {
            return m_objects.create(name, guid);
        }

        /** @brief GameObjectを破棄キューへ追加する。 */
        void destroyGameObject(GameObject* object) noexcept { m_objects.destroy(object); }

        /** @brief GameObjectを別Sceneへ移動する。 */
        bool moveGameObjectTo(GameObject* object, Scene& destination) noexcept
        {
            return m_objects.transferTo(object, destination.m_objects);
        }

        /** @brief 通常更新を実行する。 */
        void update(float deltaTime) noexcept { m_objects.update(deltaTime); }

        /** @brief 固定時間刻みの更新を実行する。 */
        void fixedUpdate(float fixedDeltaTime) noexcept { m_objects.fixedUpdate(fixedDeltaTime); }

        /** @brief 遅延更新を実行する。 */
        void lateUpdate(float deltaTime) noexcept { m_objects.lateUpdate(deltaTime); }

        /** @brief 破棄キューを処理する。 */
        void processDestroyQueue() noexcept { m_objects.processDestroyQueue(); }

        /** @brief 名前からGameObjectを検索する。 */
        GameObject* find(const std::string& name) noexcept { return m_objects.find(name); }
        /** @brief 名前からGameObjectをconstで検索する。 */
        const GameObject* find(const std::string& name) const noexcept { return m_objects.find(name); }

        /** @brief GUIDからGameObjectを検索する。 */
        GameObject* find(const ObjectGUID& guid) noexcept { return m_objects.find(guid); }
        /** @brief GUIDからGameObjectをconstで検索する。 */
        const GameObject* find(const ObjectGUID& guid) const noexcept { return m_objects.find(guid); }

        /** @brief Tagを持つ最初のGameObjectを検索する。 */
        GameObject* findWithTag(TagID tag) noexcept { return m_objects.findWithTag(tag); }

        /** @brief Tagを持つGameObjectをすべて検索する。 */
        std::vector<GameObject*> findGameObjectsWithTag(TagID tag) noexcept { return m_objects.findGameObjectsWithTag(tag); }

        /** @brief Layerに属するGameObjectをすべて検索する。 */
        std::vector<GameObject*> findGameObjectsInLayer(LayerID layer) noexcept { return m_objects.findGameObjectsInLayer(layer); }

        /** @brief Scene内のGameObject数を取得する。 */
        std::size_t getGameObjectCount() const noexcept { return m_objects.size(); }

        /** @brief Scene内のGameObject一覧を取得する。 */
        const auto& getGameObjects() const noexcept { return m_objects.objects(); }

        /** @brief Scene内のGameObjectをすべて破棄する。 */
        void clear() noexcept { m_objects.clear(); }

    private:

        ObjectGUID m_guid;           //!< SceneのGUID
        std::string m_name;          //!< Scene名
        GameObjectManager m_objects; //!< 所有するGameObjectの管理
    };
} // namespace Engine