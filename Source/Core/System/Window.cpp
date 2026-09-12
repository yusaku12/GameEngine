#include "Pch.h"
#include "Window.h"
#include "Graphics\Camera\CameraManager.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Threading\ThreadDebugStats.h"
#include "Graphics\DirectX12\Renderer.h"

namespace Engine
{
    namespace
    {
        class ScopedThreadRole
        {
        public:
            explicit ScopedThreadRole(const ThreadRole role)
                : m_previousRole(getCurrentThreadContext().role)
            {
                setCurrentThreadRole(role);
            }

            ~ScopedThreadRole()
            {
                setCurrentThreadRole(m_previousRole);
            }

            GE_DISABLE_COPY_AND_MOVE(ScopedThreadRole);

        private:
            ThreadRole m_previousRole;
        };
    }

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
        TimeManager& time = TimeManager::instance();
        SceneManager& sceneManager = SceneManager::instance();

        sceneManager.update(time.deltaTime());
        while (time.hasFixedUpdate())
        {
            sceneManager.fixedUpdate(time.fixedDeltaTime());
            time.consumeFixedUpdate();
        }
        sceneManager.lateUpdate(time.deltaTime());
        sceneManager.processDestroyQueue();
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
                runThreadedFrame();
                MainThreadDispatcher::instance().dispatchPending();
                updateTitleBar();
            }
        }

        return static_cast<int>(msg.wParam);
    }

    LRESULT Window::processMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (m_renderer.processImGuiMessage(hwnd, msg, wparam, lparam))
            return 0;

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
                else
                    CameraManager::instance().setRenderTargetSize(width, height);
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

    void Window::updateFrameInput()
    {
        TimeManager::instance().update();
        InputManager::instance().update();
    }

    void Window::runThreadedFrame()
    {
        updateFrameInput();

        JobCounter frameCounter;
        JobSystem& jobSystem = JobSystem::instance();
        jobSystem.schedule([this] { runGameUpdateJob(); }, &frameCounter);
        jobSystem.schedule([this] { runRenderUpdateJob(); }, &frameCounter);

        waitForFrameJobs(frameCounter);
        TimeManager::instance().endFrame();
    }

    void Window::runGameUpdateJob()
    {
        ScopedThreadRole role(ThreadRole::GameUpdate);
        ThreadDebugStats::ScopedTask debugTask(ThreadDebugTask::GameUpdate);
        update();
    }

    void Window::runRenderUpdateJob()
    {
        ScopedThreadRole role(ThreadRole::Render);
        ThreadDebugStats::ScopedTask debugTask(ThreadDebugTask::RenderUpdate);
        render();
    }

    void Window::waitForFrameJobs(const JobCounter& counter) const
    {
        while (!counter.isComplete())
            yieldThread();
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

        const String title = String::format(
            "GameEngine | {}x{} | FPS {:.1f}",
            rect.right - rect.left,
            rect.bottom - rect.top,
            time.getFrameRate());

        SetWindowTextW(m_hwnd, title.toWide().c_str());
    }
} // namespace Engine