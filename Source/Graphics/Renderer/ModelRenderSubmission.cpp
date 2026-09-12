#include "Pch.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"

namespace Engine
{
    ModelRenderSubmissionQueue& ModelRenderSubmissionQueue::instance() noexcept
    {
        static ModelRenderSubmissionQueue queue;
        return queue;
    }

    void ModelRenderSubmissionQueue::submit(const ModelRenderSubmission& submission)
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.push_back(submission);
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