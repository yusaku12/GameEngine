#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\Camera\CameraData.h"

namespace Engine
{
    /**
     * @brief Game updateからRender threadへCamera snapshotを渡すQueue。
     * @thread_safety Thread-safe.
     */
    class CameraRenderSubmissionQueue
    {
    public:

        /**
         * @brief QueueのSingletonを取得する。
         * @return Queueの参照。
         */
        static CameraRenderSubmissionQueue& instance() noexcept;

        CameraRenderSubmissionQueue(const CameraRenderSubmissionQueue&) = delete;
        CameraRenderSubmissionQueue& operator=(const CameraRenderSubmissionQueue&) = delete;

        /**
         * @brief 最新のCamera snapshotを提出する。
         * @param view 提出するRenderView。
         */
        void submit(const RenderView& view);

        /**
         * @brief 最新のCamera snapshotを取得する。
         * @param destination 取得先。
         * @return Snapshotを取得できた場合はtrue。
         */
        bool consume(RenderView& destination);

        /**
         * @brief 未処理のCamera snapshotを破棄する。
         */
        void clear() noexcept;

    private:

        CameraRenderSubmissionQueue() = default;
        ~CameraRenderSubmissionQueue() = default;

        std::mutex m_mutex;                  //!< Queueの排他制御用Mutex
        std::optional<RenderView> m_pending; //!< 未処理のCamera snapshot
    };
} // namespace Engine
