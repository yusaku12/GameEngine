#pragma once

#include "Core\GameObject\GameObject.h"
#include "Core\Scene\Scene.h"

namespace Engine::Serialization
{
    class PrefabSerializer;
}

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

        /**
         * @brief 空のPrefabを生成する。.
         */
        Prefab() = default;

        /**
         * @brief GameObject階層をPrefabスナップショットとして取得する。
         * @param root キャプチャするGameObjectのルート
         * @return キャプチャに成功した場合はtrue、失敗した場合はfalse
         */
        bool capture(const GameObject& root);

        /**
         * @brief PrefabをSceneへインスタンス化する。
         * @param scene インスタンス化するScene
         * @return インスタンス化されたGameObjectのポインタ
         */
        GameObject* instantiate(Scene& scene) const;

        /**
         * @brief Prefabのルートノードを取得する。
         * @return Prefabのルートノード
         */
        const PrefabNode& getRoot() const noexcept { return m_root; }

        /**
         * @brief 有効なPrefabを保持しているか判定する。
         * @return 有効なPrefabを保持している場合はtrue、そうでない場合はfalse
         */
        bool isValid() const noexcept { return m_valid; }

    private:

        friend class Serialization::PrefabSerializer;

        /**
         * @brief GameObject階層をPrefabNodeへ再帰的に変換する。
         * @param object キャプチャするGameObject
         * @return 変換されたPrefabNode
         */
        static PrefabNode captureNode(const GameObject& object);

        /**
         * @brief PrefabNodeをScene上のGameObject階層へ再帰的に変換する。
         * @param node 変換するPrefabNode
         * @param scene インスタンス化するScene
         * @param parent 親GameObject
         * @return 変換されたGameObjectのポインタ
         */
        static GameObject* instantiateNode(const PrefabNode& node, Scene& scene, GameObject* parent);

        PrefabNode m_root;    //!< ルートノード
        bool m_valid = false; //!< Prefabが有効かどうか
    };
} // namespace Engine