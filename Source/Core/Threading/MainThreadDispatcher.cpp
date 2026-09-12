#include "Pch.h"
#include "Core\Threading\MainThreadDispatcher.h"

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
        std::vector<std::function<void()>> tasks;
        {
            const std::scoped_lock lock(m_mutex);
            tasks.swap(m_tasks);
        }

        for (const auto& task : tasks)
            task();
    }
} // namespace Engine