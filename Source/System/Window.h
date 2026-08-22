#pragma once

/**
 * @brief クラスの概要
 * windowクラスは、ウィンドウの作成、更新、描画、およびメッセージ処理を行うためのクラス
 * エンジン起動時一度しか生成しない物もここで初期化する
 */
class Window
{
public:

    /**
      * @brief コンストラクタ
      */
    explicit Window(HWND hwnd);

    /**
     * @brief デストラクタ
     */
    ~Window();

    /**
     * @brief ウィンドウの更新
     */
    void update();

    /**
     * @brief ウィンドウの描画
     */
    void render();

    /**
     * @brief ウィンドウのメッセージ処理
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

    const HWND m_hwnd; //!< ウィンドウハンドル
};