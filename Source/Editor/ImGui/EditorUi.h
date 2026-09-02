#pragma once

namespace Engine
{
    /**
     * @brief Unity風のEditor DockSpaceと標準Panelを描画するクラス
     * @thread_safety Main thread only.
     */
    class EditorUi
    {
    public:
        /**
         * @brief Editorのメニューバー、DockSpace、標準Panelを描画する
         */
        void draw();

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
         * @brief EditorのDockSpaceを描画する
         */
        void drawStatusBar();

        bool m_showStats = true;  //!< Statsパネルの表示フラグ
        bool m_showGrid = true;   //!< Gridの表示フラグ
        bool m_playing = false;   //!< Playボタンの状態
        int m_selectedObject = 0; //!< 選択中のオブジェクトのID
    };
} // namespace Engine
