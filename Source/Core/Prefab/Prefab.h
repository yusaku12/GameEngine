#pragma once

#include "Core\GameObject\GameObject.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    /**
     * @brief Prefab内のGameObjectスナップショット。
     */
    struct PrefabNode
    {
        ObjectGUID sourceGUID;              //!< キャプチャ元GameObjectのGUID
        std::string name;                   //!< GameObject名
        bool active = true;                 //!< 自身のアクティブ状態
        TagID tag = 0;                      //!< Tag ID
        LayerID layer = 0;                  //!< Layer ID
        Transform localTransform;           //!< 親基準のローカルTransform
        std::vector<std::string> componentTypes; //!< 所属Componentの型名
        std::vector<PrefabNode> children;  //!< 子ノード
    };

    /**
     * @brief GameObject階層を再利用するPrefabアセット。
     * @thread_safety Main thread only.
     */
    class Prefab
    {
    public:

        /** @brief 空のPrefabを生成する。 */
        Prefab() = default;

        /** @brief GameObject階層をPrefabスナップショットとして取得する。 */
        bool capture(const GameObject& root);

        /** @brief PrefabをSceneへインスタンス化する。 */
        GameObject* instantiate(Scene& scene) const;

        /** @brief Prefabのルートノードを取得する。 */
        const PrefabNode& getRoot() const noexcept { return m_root; }

        /** @brief 有効なPrefabを保持しているか判定する。 */
        bool isValid() const noexcept { return m_valid; }

    private:

        /** @brief GameObject階層をPrefabNodeへ再帰的に変換する。 */
        static PrefabNode captureNode(const GameObject& object);

        /** @brief PrefabNodeをScene上のGameObject階層へ再帰的に変換する。 */
        static GameObject* instantiateNode(const PrefabNode& node, Scene& scene, GameObject* parent);

        PrefabNode m_root; //!< ルートノード
        bool m_valid = false; //!< Prefabが有効かどうか
    };
} // namespace Engine