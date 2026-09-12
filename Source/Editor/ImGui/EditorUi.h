#pragma once

#include "Editor\Scene\SceneDocument.h"

namespace Engine
{
    class GameObject;
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

        bool m_showShaderManager = true;               //!< Shader Managerパネルの表示フラグ
        bool m_showThreadDebug = true;                 //!< Thread Debugパネルの表示フラグ
        GameObject* m_selectedObject = nullptr;        //!< Inspectorで選択中のGameObject
        GameObject* m_hierarchyCreateParent = nullptr; //!< 作成するGameObjectの親。nullptrならRoot
        GameObject* m_hierarchyDeleteTarget = nullptr; //!< フレーム末尾に削除するGameObject
        std::array<char, 128> m_hierarchySearch{};     //!< Hierarchyの検索文字列
        std::array<char, 128> m_objectName{};          //!< Inspectorで編集中のGameObject名
        Editor::SceneDocument m_sceneDocument;         //!< 編集中Sceneのファイル状態
        bool m_hierarchyCreateRequested = false;       //!< GameObject作成要求
    };
} // namespace Engine
