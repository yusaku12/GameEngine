#pragma once

#include "Editor\Scene\SceneDocument.h"

namespace Engine
{
    class GameObject;
    class Scene;
    class ShaderManager;

    /**
     * @brief Unity風のEditor DockSpaceと標準Panelを描画するクラス
     * @thread_safety Main thread only.
     */
    class EditorUi
    {
    public:

        /**
         * @brief Editor UIを構築する。
         */
        EditorUi();

        /**
         * @brief Editorのメニューバー、DockSpace、標準Panelを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ (省略可能)
         */
        void draw(ShaderManager* shaderManager = nullptr);

    private:

        /**
         * @brief Editorで生成するGameObjectの種類。
         */
        enum class GameObjectCreateType
        {
            Empty,
            Model,
            Camera
        };

        /**
         * @brief Main threadで新規Sceneを作成する。
         */
        void newScene();

        /**
         * @brief Windows標準ダイアログからSceneを選択して読み込む。
         */
        void openScene();

        /**
         * @brief 現在の保存先へSceneを保存する。
         */
        void saveScene();

        /**
         * @brief Windows標準ダイアログから保存先を選択してSceneを保存する。
         */
        void saveSceneAs();

        /**
         * @brief 選択中のGameObject階層をPrefabとして保存する。
         */
        void saveSelectedAsPrefab();

        /**
         * @brief ファイルからPrefabを読み込み、現在のSceneへ配置する。
         */
        void instantiatePrefab();

        /**
         * @brief Editor操作に応じたGameObjectを生成する。
         * @param scene 生成先Scene。
         * @param type 生成するGameObjectの種類。
         * @param parent 親GameObject。Rootへ生成する場合はnullptr。
         * @return 生成したGameObject。失敗した場合はnullptr。
         */
        GameObject* createGameObject(Scene& scene, GameObjectCreateType type, GameObject* parent = nullptr);

        /**
         * @brief HierarchyでGameObject生成を予約する。
         * @param type 生成するGameObjectの種類。
         * @param parent 親GameObject。Rootへ生成する場合はnullptr。
         */
        void requestGameObjectCreation(GameObjectCreateType type, GameObject* parent = nullptr) noexcept;

        /**
         * @brief GameObject生成Menuを描画する。
         * @param parent 生成先の親GameObject。
         */
        void drawGameObjectCreationMenu(GameObject* parent);

        /**
         * @brief Editorのメニューバーを描画する
         */
        void drawHierarchy();

        /**
         * @brief Hierarchy内のGameObjectノードを再帰的に描画する
         */
        void drawGameObjectNode(GameObject& object);

        /**
         * @brief Inspectorで操作するGameObjectを選択する
         * @param object 選択するGameObject。選択解除時はnullptr
         */
        void selectObject(GameObject* object);

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawInspector();

        /**
         * @brief Shader Debug Window / Hot Reload 管理パネルを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ
         */
        void drawShaderManager(ShaderManager* shaderManager);

        /**
         * @brief Thread Debugパネルを描画する
         */
        void drawThreadDebug();

        bool m_showShaderManager = true;                                          //!< Shader Managerパネルの表示フラグ
        bool m_showThreadDebug = true;                                            //!< Thread Debugパネルの表示フラグ
        GameObject* m_selectedObject = nullptr;                                   //!< Inspectorで選択中のGameObject
        GameObject* m_hierarchyCreateParent = nullptr;                            //!< 作成するGameObjectの親。nullptrならRoot
        GameObject* m_hierarchyDeleteTarget = nullptr;                            //!< フレーム末尾に削除するGameObject
        std::array<char, 128> m_hierarchySearch{};                                //!< Hierarchyの検索文字列
        std::array<char, 128> m_objectName{};                                     //!< Inspectorで編集中のGameObject名
        Editor::SceneDocument m_sceneDocument;                                    //!< 編集中Sceneのファイル状態
        std::string m_prefabStatus;                                               //!< 直近のPrefab保存結果
        GameObjectCreateType m_hierarchyCreateType = GameObjectCreateType::Empty; //!< 生成予定のGameObject種別
        bool m_hierarchyCreateRequested = false;                                  //!< GameObject作成要求
    };
} // namespace Engine
