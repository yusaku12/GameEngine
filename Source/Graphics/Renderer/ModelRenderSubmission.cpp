#include "Pch.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include <atomic>

namespace Engine
{
    std::uint32_t allocateModelRenderObjectID() noexcept
    {
        static std::atomic_uint32_t nextID = 1;
        return nextID.fetch_add(1, std::memory_order_relaxed);
    }

    ModelRenderSubmissionQueue& ModelRenderSubmissionQueue::instance() noexcept
    {
        static ModelRenderSubmissionQueue queue;
        return queue;
    }

    void ModelRenderSubmissionQueue::submit(ModelRenderSubmission submission)
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.push_back(std::move(submission));
    }

    void ModelRenderSubmissionQueue::consume(std::vector<ModelRenderSubmission>& destination)
    {
        const std::scoped_lock lock(m_mutex);
        destination.clear();
        destination.swap(m_pending);
    }

    void ModelRenderSubmissionQueue::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.clear();
    }
} // namespace Engine