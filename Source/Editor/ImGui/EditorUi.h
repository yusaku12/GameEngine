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
        SceneManager m_sceneManager;
        GameObject* m_selectedObject = nullptr;
    };
} // namespace Engine
