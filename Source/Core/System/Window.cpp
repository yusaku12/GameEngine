#include "Pch.h"
#include "Window.h"
#include "Graphics\DirectX12\Renderer.h"

namespace Engine
{
    Window::Window(HWND hwnd, DX12Renderer& renderer)
        : m_hwnd(hwnd)
        , m_renderer(renderer)
    {
        TimeManager::instance().initialize();
    }

    Window::~Window()
    {
        InputManager::instance().stopAllGamepadVibration();
    }

    void Window::update()
    {
        // フレームの開始処理
        TimeManager::instance().update();

        // メモリのフレームヒープを切り替える
        MemoryManager::instance().beginFrame();

        // 入力状態を更新する
        InputManager::instance().update();
    }

    void Window::render()
    {
        if (!m_renderer.render())
            LOG_ERROR("[Window] 描画に失敗しました");
    }

    int Window::run()
    {
        MSG msg = {};

        while (WM_QUIT != msg.message)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                // 更新、描画
                update();
                render();
                updateTitleBar();
            }
        }

        return static_cast<int>(msg.wParam);
    }

    LRESULT Window::processMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
            {
                const std::uint32_t width = LOWORD(lparam);
                const std::uint32_t height = HIWORD(lparam);
                if (!m_renderer.resize(width, height))
                    LOG_ERROR("[Window] 描画領域のリサイズに失敗しました");
            }
            break;

        case WM_MOUSEWHEEL:
            InputManager::instance().addMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam));
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ACTIVATE:
            InputManager::instance().setWindowFocused(wparam != WA_INACTIVE);
            break;

        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }

        return 0;
    }

    void Window::updateTitleBar()
    {
        TimeManager& time = TimeManager::instance();

        m_titleTimer += time.getUnscaledDeltaTime();
        if (m_titleTimer < TITLE_BAR_INTERVAL)
            return;

        m_titleTimer = 0.0f;

        RECT rect{};
        GetClientRect(m_hwnd, &rect);

        const MemoryStats memory = MemoryManager::instance().getStats();

        const String title = String::format(
            "GameEngine | {}x{} | FPS {:.1f} | Mem {:.1f} / {:.0f} MiB",
            rect.right - rect.left,
            rect.bottom - rect.top,
            time.getFrameRate(),
            static_cast<double>(memory.used) / static_cast<double>(MEMORY_MIB),
            static_cast<double>(memory.capacity) / static_cast<double>(MEMORY_MIB));

        SetWindowTextW(m_hwnd, title.toWide().c_str());
    }
} // namespace Engine