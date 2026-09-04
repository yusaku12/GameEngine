#pragma once

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

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawSceneView();

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawGameView();

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawInspector();

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawProject();

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawConsole();

        /**
         * @brief Shader Debug Window / Hot Reload 管理パネルを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ
         */
        void drawShaderManager(ShaderManager* shaderManager);

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawStatusBar();

        bool m_showStats = true;          //!< Statsパネルの表示フラグ
        bool m_showGrid = true;           //!< Gridの表示フラグ
        bool m_showShaderManager = true;  //!< Shader Managerパネルの表示フラグ
        bool m_playing = false;           //!< Playボタンの状態
        int m_selectedObject = 0;         //!< 選択中のオブジェクトのID
    };
} // namespace Engine
