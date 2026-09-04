#include "Pch.h"
#include "Core\System\Window.h"
#include "Core\GameObject\GameObjectManager.h"
#include "Graphics\DirectX12\Renderer.h"

namespace Engine
{
    static constexpr LONG SCREEN_WIDTH = static_cast<LONG>(1280); //!< 画面の幅
    static constexpr LONG SCREEN_HEIGHT = static_cast<LONG>(720); //!< 画面の高さ
    static constexpr LPCWSTR TITLE = L"GameEngine";               //!< ウィンドウのタイトル
    static constexpr LPCWSTR WINDOW_CLASS = L"GameEngineWindow";  //!< ウィンドウクラス名

    /**
     * @brief ウィンドウプロシージャ
     * @param hwnd ウィンドウハンドル
     * @param msg メッセージID
     * @param wparam メッセージの追加情報
     * @param lparam メッセージの追加情報
     * @return LRESULT メッセージ処理の結果
     */
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        return window != nullptr ? window->processMessage(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
    }

    /**
     * @brief コンソールの初期化
     */
    static void initializeConsole()
    {
        if (!AllocConsole())
            return;

        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);

        SetConsoleTitleW(L"GameEngine Console");
        SetConsoleOutputCP(CP_UTF8);
    }

    /**
     * @brief エンジンの基盤機能を初期化する
     * @return bool 成功したらtrue
     */
    static bool initializeCore()
    {
        // コンソールの初期化
        initializeConsole();

        // メインスレッドの役割を設定
        setCurrentThreadRole(ThreadRole::Main);

        // ロガーの初期化
        if (!Logger::instance().initialize())
            return false;

        // メモリマネージャの初期化
        if (!MemoryManager::instance().initialize())
        {
            // ロガーの終了処理を行うことで、初期化に失敗した原因をログに出力する
            Logger::instance().finalize();
            return false;
        }

        // ジョブシステムの初期化
        JobSystem::instance().initialize();

        return true;
    }

    /**
     * @brief エンジンの基盤機能を終了する
     */
    static void finalizeCore()
    {
        // ジョブシステムの終了処理を行うことで、ジョブの完了待ち中に発生したエラーをログに出力する
        JobSystem::instance().finalize();

        // Windowの破棄後に呼び出すことで、未解放のメモリをリークとして検出する
        MemoryManager::instance().finalize();

        // ロガーの終了処理を行うことで、未書き出しのログを出力する
        Logger::instance().finalize();
    }

    /**
     * @brief エンジンを起動する
     * @param instance インスタンスハンドル
     * @param cmdShow ウィンドウ表示方法
     * @return int 終了コード
     */
    static int runEngine(HINSTANCE instance, INT cmdShow)
    {
        if (!initializeCore())
            return -1;

        DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
        DWORD exStyle = WS_EX_APPWINDOW;

        RECT rect{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
        ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;

        WNDCLASSEX windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = instance;
        windowClass.hIcon = LoadIcon(instance, MAKEINTRESOURCEW(111));
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = WINDOW_CLASS;

        if (!RegisterClassEx(&windowClass))
        {
            LOG_CRITICAL("[Engine] ウィンドウクラスの登録に失敗しました");
            finalizeCore();
            return -1;
        }

        HWND hwnd = ::CreateWindowEx(
            exStyle,
            WINDOW_CLASS,
            TITLE,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            nullptr, nullptr,
            instance,
            nullptr);

        if (hwnd == nullptr)
        {
            LOG_CRITICAL("[Engine] ウィンドウの作成に失敗しました");
            finalizeCore();
            return -1;
        }

        ShowWindow(hwnd, cmdShow);

        int result = 0;
        {
            // 描画スレッドの役割を設定
            setCurrentThreadRole(ThreadRole::Render);

            // DirectX 12 レンダラーの初期化
            DX12Renderer renderer;
            if (!renderer.initialize(hwnd, SCREEN_WIDTH, SCREEN_HEIGHT))
            {
                // 描画スレッドの役割をメインスレッドに戻す
                setCurrentThreadRole(ThreadRole::Main);
                DestroyWindow(hwnd);
                finalizeCore();
                return -1;
            }

            Window window(hwnd, renderer);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&window));

            GameObjectManager gameObjectManager;
            const GameObjectHandle movingObject = gameObjectManager.Create("Mover");
            auto* mover = gameObjectManager.Get(movingObject);
            if (mover != nullptr)
            {
                mover->SetPosition(Vector3::Zero);
                mover->SetVelocity(Vector3(1.0f, 0.0f, 0.0f));
            }

            gameObjectManager.Update(1.0f / 60.0f);

            result = window.run();
            renderer.finalize();

            // 描画スレッドの役割をメインスレッドに戻す
            setCurrentThreadRole(ThreadRole::Main);
        }

        finalizeCore();
        return result;
    }
} // namespace Engine

/**
 * @brief エントリーポイント
 * @param instance インスタンスハンドル
 * @param prevInstance 前のインスタンスハンドル（未使用）
 * @param cmdLine コマンドライン引数（未使用）
 * @param cmdShow ウィンドウ表示方法
 * @return int 終了コード
 */
INT WINAPI wWinMain(
    HINSTANCE instance,
    [[maybe_unused]] HINSTANCE prevInstance,
    [[maybe_unused]] LPWSTR cmdLine,
    INT cmdShow)
{
    return Engine::runEngine(instance, cmdShow);
}