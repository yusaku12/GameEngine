#pragma once

#include "Core\CoreDefines.h"
#include "Core\GameObject\GameObject.h"

namespace Engine
{
    /**
     * @brief GameObjectの生成・GUID検索・遅延破棄を管理するクラス。
     * @thread_safety Main thread only. Worker threadからの構造変更は禁止します。
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
         * @return 生成されたGameObjectのポインタ。
         */
        GameObject* create(const std::string& name = "GameObject");
        /** @brief GUIDを指定してGameObjectを生成する。 */
        GameObject* create(const std::string& name, ObjectGUID guid);

        /**
         * @brief GameObjectを破棄する。
         * @details 破棄は遅延処理され、次のprocessDestroyQueue()呼び出し時に実行されます。
         * @param object 破棄するGameObjectのポインタ。
         */
        void destroy(GameObject* object) noexcept;

        /**
         * @brief GameObjectを別のGameObjectManagerに移動する。
         * @details 移動は遅延処理され、次のprocessDestroyQueue()呼び出し時に実行されます。
         * @param object 移動するGameObjectのポインタ。
         * @param destination 移動先のGameObjectManager。
         * @return 移動が成功した場合はtrue、失敗した場合はfalse。
         */
        bool transferTo(GameObject* object, GameObjectManager& destination) noexcept;

        /**
         * @brief 破棄キューに溜まったGameObjectを処理する。
         * @details destroy()やtransferTo()で遅延破棄されたGameObjectがここで実際に破棄されます。
         */
        void processDestroyQueue() noexcept;

        /**
         * @brief 全てのGameObjectを破棄する。
         */
        void clear() noexcept;

        /**
         * @brief 全てのGameObjectを更新する。
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
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void lateUpdate(float deltaTime) noexcept;

        /**
         * @brief 名前でGameObjectを検索する。
         * @param name 検索するGameObjectの名前。
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* find(const std::string& name) noexcept;
        /** @brief 名前でGameObjectをconst検索する。 */
        const GameObject* find(const std::string& name) const noexcept;

        /**
         * @brief GUIDでGameObjectを検索する。
         * @param guid 検索するGameObjectのGUID。
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* find(const ObjectGUID& guid) noexcept;
        /** @brief GUIDでGameObjectをconst検索する。 */
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
         * @brief 全てのGameObjectを取得する。
         * @return GameObjectのポインタの配列。
         */
        const auto& objects() const noexcept { return m_objects; }

    private:

        std::vector<std::unique_ptr<GameObject>> m_objects; //!< 管理しているGameObjectの配列
        std::vector<GameObject*> m_destroyQueue;            //!< 破棄キューに溜まったGameObjectの配列
        std::unordered_map<ObjectGUID, GameObject*, ObjectGUIDHash> m_guidIndex; //!< GUIDでGameObjectを検索するためのインデックス
    };
} // namespace Engine