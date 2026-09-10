#pragma once

#include <memory>

#include "Core\Scene\SceneManager.h"

namespace Engine
{
    class ShaderManager;

    /**
     * @brief Unity風のEditor DockSpaceと標準Panelを描画するクラス
     * @thread_safety Main thread only.
     */
    class EditorUi
    {
    public:
        /** @brief Editor UIを構築する。 */
        EditorUi();

        /**
         * @brief Editorのメニューバー、DockSpace、標準Panelを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ (省略可能)
         */
        void draw(ShaderManager* shaderManager = nullptr);

    private:

        /**
         * @brief Editorのメニューバーを描画する
         */
        void drawHierarchy();
        /** @brief Hierarchy内のGameObjectノードを再帰的に描画する。 */
        void drawGameObjectNode(GameObject& object);

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawInspector();

        /**
         * @brief Shader Debug Window / Hot Reload 管理パネルを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ
         */
        void drawShaderManager(ShaderManager* shaderManager);

        bool m_showShaderManager = true;  //!< Shader Managerパネルの表示フラグ
        SceneManager m_sceneManager;      //!< Editorが表示するSceneの管理
        GameObject* m_selectedObject = nullptr; //!< Inspectorで選択中のGameObject
    };
} // namespace Engine
