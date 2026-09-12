#include "Pch.h"
#include "Graphics\Camera\CameraRenderSubmission.h"

namespace Engine
{
    CameraRenderSubmissionQueue& CameraRenderSubmissionQueue::instance() noexcept
    {
        static CameraRenderSubmissionQueue instance;
        return instance;
    }

    void CameraRenderSubmissionQueue::submit(const RenderView& view)
    {
        const std::scoped_lock lock(m_mutex);
        m_pending = view;
    }

    bool CameraRenderSubmissionQueue::consume(RenderView& destination)
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_pending.has_value())
            return false;

        destination = *m_pending;
        m_pending.reset();
        return true;
    }

    void CameraRenderSubmissionQueue::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.reset();
    }
} // namespace Engine