#include "Pch.h"

#include "Window.h"

namespace Engine
{
    //! タイトルバーを更新する間隔（秒）
    static constexpr float TITLE_BAR_INTERVAL = 0.5f;

    Window::Window(HWND hwnd)
        : m_hwnd(hwnd)
    {
        TimeManager::instance().initialize();
    }

    Window::~Window()
    {
    }

    void Window::update()
    {
    }

    void Window::render()
    {
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
                // フレームの開始処理
                TimeManager::instance().update();
                MemoryManager::instance().beginFrame();

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
            }
            break;

        case WM_MOUSEWHEEL:
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ACTIVATE:
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