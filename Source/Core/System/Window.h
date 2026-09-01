#pragma once

namespace Engine
{
    class DX12Renderer;

    /**
     * @brief ウィンドウクラス
     * ウィンドウの作成、更新、描画、およびメッセージ処理を行う
     * エンジン起動時に一度しか生成しない物もここで初期化する
     */
    class Window
    {
    public:

        /**
         * @brief コンストラクタ
         * @param hwnd ウィンドウハンドル
         */
        Window(HWND hwnd, DX12Renderer& renderer);

        /**
         * @brief デストラクタ
         */
        ~Window();

        GE_DISABLE_COPY_AND_MOVE(Window);

        /**
         * @brief ウィンドウの更新
         */
        void update();

        /**
         * @brief ウィンドウの描画
         */
        void render();

        /**
         * @brief メインループを実行する
         * @return int 終了コード
         */
        int run();

        /**
         * @brief ウィンドウのメッセージ処理
         * @param hwnd ウィンドウハンドル
         * @param msg メッセージID
         * @param wparam メッセージの追加情報
         * @param lparam メッセージの追加情報
         * @return LRESULT メッセージ処理の結果
         */
        LRESULT processMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    private:

        /**
         * @brief ウィンドウのタイトルバーを更新する
         */
        void updateTitleBar();

        static constexpr float TITLE_BAR_INTERVAL = 0.5f; //!< タイトルバーを更新する間隔（秒）

        const HWND m_hwnd;               //!< ウィンドウハンドル
        DX12Renderer& m_renderer;        //!< 描画処理
        float      m_titleTimer = 0.0f;  //!< タイトルバーを更新するまでの経過時間
    };
} // namespace Engine
