#include "Pch.h"
#include "Window.h"
#include "Graphics\Camera\CameraManager.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Threading\ThreadDebugStats.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Physics\PhysicsSystem.h"
#include "Core\System\GameSession.h"

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
        GameSession::instance().update();
    }

    void Window::render()
    {
        m_renderer.setEditorOverlayEnabled(!GameSession::instance().isPlaying());
        if (!m_renderer.render())
        {
            LOG_ERROR("[Window] 描画に失敗しました");
            m_frameFailed = true;
        }
    }

    int Window::run()
    {
        MSG msg = {};

        while (WM_QUIT != msg.message && !m_frameFailed)
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

        return m_frameFailed ? -1 : static_cast<int>(msg.wParam);
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
                {
                    LOG_ERROR("[Window] 描画領域のリサイズに失敗しました");
                    m_frameFailed = true;
                }
            }
            break;

        case WM_MOUSEWHEEL:
            InputManager::instance().addMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam));
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ACTIVATE:
            m_windowFocused = LOWORD(wparam) != WA_INACTIVE;
            if (!m_windowFocused)
                InputManager::instance().setWindowFocused(false);
            break;

        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }

        return 0;
    }

    void Window::updateFrameInput()
    {
        GameSession::instance().beginFrame(m_windowFocused);
    }

    void Window::runThreadedFrame()
    {
        const auto frameStart = std::chrono::steady_clock::now();
        const std::uint64_t frameId = FrameProfiler::instance().beginFrame();
        if (frameId == 0)
            return;

        updateFrameInput();

        JobSystem& jobSystem = JobSystem::instance();
        JobCounter updateCounter;
        jobSystem.schedule([this] { runGameUpdateJob(); }, &updateCounter);
        waitForFrameJobs(updateCounter);

        JobCounter renderCounter;
        jobSystem.schedule([this] { runRenderUpdateJob(); }, &renderCounter);
        waitForFrameJobs(renderCounter);
        const std::array<std::uint32_t, 2> gameViewSize = m_renderer.getGameViewSize();
        CameraManager::instance().setRenderTargetSize(gameViewSize[0], gameViewSize[1]);
        TimeManager::instance().endFrame();

        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frameStart);
        FrameProfiler::instance().endFrame(frameId, elapsed.count());
    }

    void Window::runGameUpdateJob()
    {
        ScopedThreadRole role(ThreadRole::GameUpdate);
        ThreadDebugStats::ScopedTask debugTask(ThreadDebugTask::GameUpdate);
        FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::GameUpdate);
        update();
    }

    void Window::runRenderUpdateJob()
    {
        ScopedThreadRole role(ThreadRole::Render);
        ThreadDebugStats::ScopedTask debugTask(ThreadDebugTask::RenderUpdate);
        FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::RenderUpdate);
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