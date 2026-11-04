#include "Pch.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    MainThreadDispatcher& MainThreadDispatcher::instance() noexcept
    {
        static MainThreadDispatcher dispatcher;
        return dispatcher;
    }

    void MainThreadDispatcher::post(std::function<void()> task)
    {
        if (!task)
            return;

        const std::scoped_lock lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    void MainThreadDispatcher::dispatchPending()
    {
        if (m_isDispatching)
        {
            LOG_ERROR("[MainThreadDispatcher] Cannot re-enter pending task dispatch.");
            return;
        }
        {
            const std::scoped_lock lock(m_mutex);
            m_dispatchTasks.swap(m_tasks);
        }
        m_isDispatching = true;
        struct DispatchScope
        {
            bool& active;
            std::vector<std::function<void()>>& tasks;
            ~DispatchScope() { tasks.clear(); active = false; }
        } dispatchScope{ m_isDispatching, m_dispatchTasks };

        for (const auto& task : m_dispatchTasks)
        {
            try
            {
                task();
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR("[MainThreadDispatcher] Task failed with an exception: {}", exception.what());
            }
            catch (...)
            {
                LOG_ERROR("[MainThreadDispatcher] Task failed with an unknown exception.");
            }
        }
    }
} // namespace Engine