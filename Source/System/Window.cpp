#include "Pch.h"
#include "Window.h"

Window::Window(HWND hwnd)
    : m_hwnd(hwnd)
{
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
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
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
        }
        break;

    case WM_MOUSEWHEEL:
    {
    }
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
    //RECT rc{};
    //GetClientRect(m_hwnd, &rc);

    //const int width = rc.right - rc.left;
    //const int height = rc.bottom - rc.top;

    //wchar_t text[256];
    //swprintf_s(text,
    //    L"DX12 | %dx%d | FPS: %.1f",
    //    width,
    //    height,
    //    static_cast<float>(TimeManager::Instance().getFPS()));

    //SetWindowTextW(m_hwnd, text);
}